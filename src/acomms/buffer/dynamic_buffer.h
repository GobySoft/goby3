// Copyright 2009-2018 Toby Schneider (http://gobysoft.org/index.wt/people/toby)
//                     GobySoft, LLC (2013-)
//                     Massachusetts Institute of Technology (2007-2014)
//                     Community contributors (see AUTHORS file)
//
//
// This file is part of the Goby Underwater Autonomy Project Libraries
// ("The Goby Libraries").
//
// The Goby Libraries are free software: you can redistribute them and/or modify
// them under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 2.1 of the License, or
// (at your option) any later version.
//
// The Goby Libraries are distributed in the hope that they will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with Goby.  If not, see <http://www.gnu.org/licenses/>.

#include <deque>
#include <type_traits>
#include <unordered_map>
#include <vector>

#include "goby/acomms/acomms_constants.h"
#include "goby/acomms/protobuf/buffer.pb.h"
#include "goby/exception.h"
#include "goby/time/convert.h"
#include "goby/time/steady_clock.h"
#include "goby/util/debug_logger.h"

namespace goby
{
namespace acomms
{
class DynamicBufferNoDataException : goby::Exception
{
  public:
    DynamicBufferNoDataException() : goby::Exception("No queues have data available") {}
    ~DynamicBufferNoDataException() {}
};

template <typename Container> size_t data_size(const Container& c) { return c.size(); }

/// Represents a time-dependent priority queue for a single group of messages (e.g. for a single DCCL ID)
template <typename T> class DynamicSubBuffer
{
  public:
    using size_type = typename std::deque<T>::size_type;

    struct Value
    {
        goby::time::SteadyClock::time_point push_time;
        T data;
        bool operator==(const Value& a) const { return a.push_time == push_time && a.data == data; }
    };

    /// \brief Create a subbuffer with the given configuration
    DynamicSubBuffer(const goby::acomms::protobuf::DynamicBufferConfig& cfg)
        : DynamicSubBuffer(std::vector<goby::acomms::protobuf::DynamicBufferConfig>(1, cfg))
    {
    }

    /// \brief Create a subbuffer merging two or more configuration objects
    ///
    /// These configurations are merged using the following rules:
    /// - `ttl` and `value_base` are averaged
    /// - `ack_required:` true takes priority over false
    /// - `newest_first:` true takes priority over false
    /// - `blackout_time:` the smaller value takes precedence
    /// - `queue_size:` the larger value takes precedence
    DynamicSubBuffer(const std::vector<goby::acomms::protobuf::DynamicBufferConfig>& cfgs)
    {
        using goby::acomms::protobuf::DynamicBufferConfig;

        if (cfgs.empty())
            throw(goby::Exception("Configuration vector must not be empty for DynamicSubBuffer"));

        // extract these types from the Protobuf message
        using ttl_type =
            std::result_of<decltype (&DynamicBufferConfig::ttl)(DynamicBufferConfig)>::type;
        using value_base_type =
            std::result_of<decltype (&DynamicBufferConfig::value_base)(DynamicBufferConfig)>::type;

        ttl_type ttl_sum = 0;
        ttl_type ttl_divisor = 0;
        value_base_type value_base_sum = 0;
        value_base_type value_base_divisor = 0;

        for (const auto& cfg : cfgs)
        {
            if (cfg.has_ack_required() && (!cfg_.has_ack_required() || cfg.ack_required()))
                cfg_.set_ack_required(cfg.ack_required());
            if (cfg.has_blackout_time() &&
                (!cfg_.has_blackout_time() || cfg.blackout_time() < cfg_.blackout_time()))
                cfg_.set_blackout_time(cfg.blackout_time());
            if (cfg.has_max_queue() &&
                (!cfg_.has_max_queue() || cfg.max_queue() > cfg_.max_queue()))
                cfg_.set_max_queue(cfg.max_queue());
            if (cfg.has_newest_first() && (!cfg_.has_newest_first() || cfg.newest_first()))
                cfg_.set_newest_first(cfg.newest_first());

            if (cfg.has_ttl())
            {
                ttl_sum += cfg.ttl();
                ++ttl_divisor;
            }

            if (cfg.has_value_base())
            {
                value_base_sum += cfg.value_base();
                ++value_base_divisor;
            }
        }

        if (ttl_divisor > 0)
            cfg_.set_ttl(ttl_sum / ttl_divisor);
        if (value_base_divisor > 0)
            cfg_.set_value_base(value_base_sum / value_base_divisor);
    }

    ~DynamicSubBuffer() {}

    /// \brief Return the aggregate configuration
    const goby::acomms::protobuf::DynamicBufferConfig& cfg() const { return cfg_; }

    /// \brief Returns the value at the top of the queue that hasn't been sent wihin ack_timeout
    ///
    /// \param reference Current time reference (defaults to now)
    /// \param ack_timeout Duration to wait before resending a value
    /// \return Value to send
    /// \throw DynamicBufferNoDataException no data to (re)send
    Value& top(goby::time::SteadyClock::time_point reference = goby::time::SteadyClock::now(),
               goby::time::SteadyClock::duration ack_timeout = std::chrono::microseconds(0))
    {
        for (auto& datum_pair : data_)
        {
            auto& datum_last_access = datum_pair.first;
            if (datum_last_access == zero_point_ || datum_last_access + ack_timeout < reference)
            {
                last_access_ = reference;
                datum_last_access = last_access_;
                return datum_pair.second;
            }
        }
        throw(DynamicBufferNoDataException());
    }

    /// \brief returns true if all messages have been sent within ack_timeout of the reference provided and thus none are available for resending yet
    bool all_waiting_for_ack(
        goby::time::SteadyClock::time_point reference = goby::time::SteadyClock::now(),
        goby::time::SteadyClock::duration ack_timeout = std::chrono::microseconds(0)) const
    {
        for (const auto& datum_pair : data_)
        {
            const auto& datum_last_access = datum_pair.first;
            if (datum_last_access == zero_point_ || datum_last_access + ack_timeout < reference)
                return false;
        }
        return true;
    }

    enum class ValueResult
    {
        VALUE_PROVIDED,
        EMPTY,
        IN_BLACKOUT,
        NEXT_MESSAGE_TOO_LARGE,
        ALL_MESSAGES_WAITING_FOR_ACK
    };

    /// \brief Provides the numerical priority value based on this subbuffer's base priority, time-to-live (ttl) and time since last access (last call to top())
    ///
    /// \param reference time point to use for current reference when calculating this priority value (defaults to current time)
    /// \param max_bytes the maximum number of bytes requested
    /// \param ack_timeout how long to wait before resending the same value again
    /// \return priority value for this sub buffer
    std::pair<double, ValueResult>
    top_value(goby::time::SteadyClock::time_point reference = goby::time::SteadyClock::now(),
              size_type max_bytes = std::numeric_limits<size_type>::max(),
              goby::time::SteadyClock::duration ack_timeout = std::chrono::microseconds(0)) const
    {
        if (empty())
            return std::make_pair(-std::numeric_limits<double>::infinity(), ValueResult::EMPTY);
        else if (in_blackout(reference))
            return std::make_pair(-std::numeric_limits<double>::infinity(),
                                  ValueResult::IN_BLACKOUT);
        else if (data_size(data_.front().second.data) > max_bytes)
            return std::make_pair(-std::numeric_limits<double>::infinity(),
                                  ValueResult::NEXT_MESSAGE_TOO_LARGE);
        else if (all_waiting_for_ack(reference, ack_timeout))
            return std::make_pair(-std::numeric_limits<double>::infinity(),
                                  ValueResult::ALL_MESSAGES_WAITING_FOR_ACK);

        using Duration = std::chrono::microseconds;

        double dt = std::chrono::duration_cast<Duration>(reference - last_access_).count();
        double ttl = goby::time::convert_duration<Duration>(cfg_.ttl_with_units()).count();
        double v_b = cfg_.value_base();

        double v = v_b * dt / ttl;
        return std::make_pair(v, ValueResult::VALUE_PROVIDED);
    }

    /// \brief Returns if buffer is in blackout
    ///
    /// \param reference time point to use for current reference when calculating blackout
    bool in_blackout(
        goby::time::SteadyClock::time_point reference = goby::time::SteadyClock::now()) const
    {
        auto blackout = goby::time::convert_duration<goby::time::SteadyClock::duration>(
            cfg_.blackout_time_with_units());

        return reference <= (last_access_ + blackout);
    }
    /// \brief Returns if this queue is empty
    bool empty() const { return data_.empty(); }

    /// \brief Retrieves the size of the queue
    size_type size() const { return data_.size(); }

    /// \brief Pop the value on the top of the queue
    void pop() { data_.pop_front(); }

    /// \brief Push a value to the queue
    ///
    /// \param t Value to push
    /// \param reference Reference time to use for this value (defaults to current time)
    /// \return vector of values removed due to max_queue being exceeded
    std::vector<Value>
    push(const T& t, goby::time::SteadyClock::time_point reference = goby::time::SteadyClock::now())
    {
        std::vector<Value> exceeded;

        if (cfg_.newest_first())
            data_.push_front(std::make_pair(zero_point_, Value({reference, t})));
        else
            data_.push_back(std::make_pair(zero_point_, Value({reference, t})));

        if (data_.size() > cfg_.max_queue())
        {
            exceeded.push_back(data_.back().second);
            data_.pop_back();
        }
        return exceeded;
    }

    /// \brief Erase any values that have exceeded their time-to-live
    ///
    /// \return Vector of values that have expired and have been erased
    std::vector<Value>
    expire(goby::time::SteadyClock::time_point reference = goby::time::SteadyClock::now())
    {
        std::vector<Value> expired;

        auto ttl =
            goby::time::convert_duration<goby::time::SteadyClock::duration>(cfg_.ttl_with_units());
        if (cfg_.newest_first())
        {
            while (!data_.empty() && reference > (data_.back().second.push_time + ttl))
            {
                expired.push_back(data_.back().second);
                data_.pop_back();
            }
        }
        else
        {
            while (!data_.empty() && reference > (data_.front().second.push_time + ttl))
            {
                expired.push_back(data_.front().second);
                data_.pop_front();
            }
        }
        return expired;
    }

    /// \brief Erase a value
    ///
    /// \param value Value to erase (if it exists)
    /// \return true if the value was found and erase, false if the value was not found
    bool erase(const Value& value)
    {
        // start at the beginning as we are most likely to want to erase elements we recently asked for with top()

        for (auto it = data_.begin(), end = data_.end(); it != end; ++it)
        {
            const auto& datum_pair = it->second;
            if (datum_pair == value)
            {
                data_.erase(it);
                return true;
            }

            // if these are true, we're not going to find it so stop looking
            if (cfg_.newest_first() && datum_pair.push_time < value.push_time)
                break;
            else if (!cfg_.newest_first() && datum_pair.push_time > value.push_time)
                break;
        }
        return false;
    }

  private:
    goby::acomms::protobuf::DynamicBufferConfig cfg_;

    // pair of last send -> value
    std::deque<std::pair<goby::time::SteadyClock::time_point, Value> > data_;
    goby::time::SteadyClock::time_point last_access_{goby::time::SteadyClock::now()};

    goby::time::SteadyClock::time_point zero_point_{std::chrono::seconds(0)};
};

/// Represents a time-dependent priority queue for several groups of messages (multiple DynamicSubBuffers)
template <typename T> class DynamicBuffer
{
  public:
    DynamicBuffer()
    {
        glog_priority_group_ = "goby::acomms::buffer::priority";
        goby::glog.add_group(glog_priority_group_, util::Colors::yellow);
    }
    ~DynamicBuffer() {}

    using subbuffer_id_type = std::string;
    using size_type = typename DynamicSubBuffer<T>::size_type;
    using modem_id_type = int;

    struct Value
    {
        modem_id_type modem_id;
        subbuffer_id_type subbuffer_id;
        goby::time::SteadyClock::time_point push_time;
        T data;
    };

    /// \brief Create a new subbuffer with the given configuration
    ///
    /// This must be called before using functions that reference this subbuffer ID (e.g. push(...), erase(...))
    /// \param dest_id The modem id destination for these messages
    /// \param sub_id An identifier for this subbuffer
    /// \param cfg The configuration for this new subbuffer
    void create(modem_id_type dest_id, const subbuffer_id_type& sub_id,
                const goby::acomms::protobuf::DynamicBufferConfig& cfg)
    {
        create(dest_id, sub_id, std::vector<goby::acomms::protobuf::DynamicBufferConfig>(1, cfg));
    }

    /// \brief Create a new subbuffer merging the given configuration (See DynamicSubBuffer() for details)
    ///
    /// This must be called before using functions that reference this subbuffer ID (e.g. push(...), erase(...))
    /// \param dest_id The modem id destination for these messages
    /// \param sub_id An identifier for this subbuffer
    /// \param cfgs The configuration for this new subbuffer
    void create(modem_id_type dest_id, const subbuffer_id_type& sub_id,
                const std::vector<goby::acomms::protobuf::DynamicBufferConfig>& cfgs)
    {
        if (sub_.count(dest_id) && sub_.at(dest_id).count(sub_id))
            throw(goby::Exception("Subbuffer ID: " + sub_id + " already exists."));

        sub_[dest_id].insert(std::make_pair(sub_id, DynamicSubBuffer<T>(cfgs)));
    }

    /// \brief Replace an existing subbuffer with the given configuration (any messages in the subbuffer will be erased)
    ///
    /// \param dest_id The modem id destination for these messages
    /// \param sub_id An identifier for this subbuffer
    /// \param cfg The configuration for this replacement subbuffer
    void replace(modem_id_type dest_id, const subbuffer_id_type& sub_id,
                 const goby::acomms::protobuf::DynamicBufferConfig& cfg)
    {
        replace(dest_id, sub_id, std::vector<goby::acomms::protobuf::DynamicBufferConfig>(1, cfg));
    }

    /// \brief Create a new subbuffer merging the given configuration (See DynamicSubBuffer() for details)
    ///
    /// This must be called before using functions that reference this subbuffer ID (e.g. push(...), erase(...))
    /// \param dest_id The modem id destination for these messages
    /// \param sub_id An identifier for this subbuffer
    /// \param cfgs The configuration for this new subbuffer
    void replace(modem_id_type dest_id, const subbuffer_id_type& sub_id,
                 const std::vector<goby::acomms::protobuf::DynamicBufferConfig>& cfgs)
    {
        sub_[dest_id].erase(sub_id);
        create(dest_id, sub_id, cfgs);
    }

    /// \brief Push a new message to the buffer
    ///
    /// \param fvt Full tuple giving subbuffer id, time, and value
    /// \return vector of values removed due to max_queue being exceeded
    /// \throw goby::Exception If subbuffer doesn't exist
    std::vector<Value> push(const Value& fvt)
    {
        std::vector<Value> exceeded;
        auto sub_exceeded = sub(fvt.modem_id, fvt.subbuffer_id).push(fvt.data, fvt.push_time);
        for (const auto& e : sub_exceeded)
            exceeded.push_back({fvt.modem_id, fvt.subbuffer_id, e.push_time, e.data});
        return exceeded;
    }

    /// \brief Is this buffer empty (that is, are all subbuffers empty)?
    bool empty() const
    {
        for (const auto& sub_id_p : sub_)
        {
            for (const auto& sub_p : sub_id_p.second)
            {
                if (!sub_p.second.empty())
                    return false;
            }
        }

        return true;
    }

    /// \brief Size of the buffer (that is, sum of the subbuffer sizes)
    size_type size() const
    {
        size_type size = 0;
        for (const auto& sub_id_p : sub_)
        {
            for (const auto& sub_p : sub_id_p.second) size += sub_p.second.size();
        }
        return size;
    }

    /// \brief Returns the top value in a priority contest between all subbuffers
    ///
    /// \param dest_id Modem id for this packet (can be QUERY_DESTINATION_ID to query all possible destinations)
    /// \param max_bytes Maximum number of bytes in the returned message
    /// \param ack_timeout Duration to wait before resending a value
    /// \return Value with the highest priority (DynamicSubBuffer::top_value()) of all the subbuffers
    Value top(modem_id_type dest_id = goby::acomms::QUERY_DESTINATION_ID,
              size_type max_bytes = std::numeric_limits<size_type>::max(),
              goby::time::SteadyClock::duration ack_timeout = std::chrono::microseconds(0))
    {
        using goby::glog;

        glog.is_debug1() && glog << group(glog_priority_group_)
                                 << "Starting priority contest:" << std::endl;

        typename std::unordered_map<subbuffer_id_type, DynamicSubBuffer<T> >::iterator winning_sub;
        double winning_value = -std::numeric_limits<double>::infinity();

        auto now = goby::time::SteadyClock::now();

        if (dest_id != goby::acomms::QUERY_DESTINATION_ID && !sub_.count(dest_id))
            throw(DynamicBufferNoDataException());

        // if QUERY_DESTINATION_ID, search all subbuffers, otherwise just search the ones that were specified by dest_id
        for (auto sub_id_it = (dest_id == goby::acomms::QUERY_DESTINATION_ID) ? sub_.begin()
                                                                              : sub_.find(dest_id),
                  sub_id_end = (dest_id == goby::acomms::QUERY_DESTINATION_ID)
                                   ? sub_.end()
                                   : ++sub_.find(dest_id);
             sub_id_it != sub_id_end; ++sub_id_it)
        {
            for (auto sub_it = sub_id_it->second.begin(), sub_end = sub_id_it->second.end();
                 sub_it != sub_end; ++sub_it)
            {
                double value;
                typename DynamicSubBuffer<T>::ValueResult result;
                std::tie(value, result) = sub_it->second.top_value(now, max_bytes, ack_timeout);

                std::string value_or_reason;
                switch (result)
                {
                    case DynamicSubBuffer<T>::ValueResult::VALUE_PROVIDED:
                        value_or_reason = std::to_string(value);
                        break;

                    case DynamicSubBuffer<T>::ValueResult::EMPTY: value_or_reason = "empty"; break;

                    case DynamicSubBuffer<T>::ValueResult::IN_BLACKOUT:
                        value_or_reason = "blackout";
                        break;

                    case DynamicSubBuffer<T>::ValueResult::NEXT_MESSAGE_TOO_LARGE:
                        value_or_reason = "too large";
                        break;

                    case DynamicSubBuffer<T>::ValueResult::ALL_MESSAGES_WAITING_FOR_ACK:
                        value_or_reason = "ack wait";
                        break;
                }

                glog.is_debug1() && glog << group(glog_priority_group_) << "\t" << sub_it->first
                                         << " [dest: " << sub_id_it->first
                                         << ", n: " << sub_it->second.size()
                                         << "]: " << value_or_reason << std::endl;

                if (value > winning_value)
                {
                    winning_value = value;
                    winning_sub = sub_it;
                    dest_id = sub_id_it->first;
                }
            }
        }

        if (winning_value == -std::numeric_limits<double>::infinity())
            throw(DynamicBufferNoDataException());

        glog.is_debug1() && glog << group(glog_priority_group_) << "Winner: " << winning_sub->first
                                 << std::endl;

        const auto& top_p = winning_sub->second.top(now, ack_timeout);
        return {dest_id, winning_sub->first, top_p.push_time, top_p.data};
    }

    /// \brief Erase a value
    ///
    /// \param value Value to erase (if it exists)
    /// \return true if the value was found and erase, false if the value was not found
    /// \throw goby::Exception If subbuffer doesn't exist
    bool erase(const Value& value)
    {
        return sub(value.modem_id, value.subbuffer_id).erase({value.push_time, value.data});
    }

    /// \brief Erase any values that have exceeded their time-to-live
    ///
    /// \return Vector of values that have expired and have been erased
    std::vector<Value> expire()
    {
        auto now = goby::time::SteadyClock::now();
        std::vector<Value> expired;
        for (auto& sub_id_p : sub_)
        {
            for (auto& sub_p : sub_id_p.second)
            {
                auto sub_expired = sub_p.second.expire(now);
                for (const auto& e : sub_expired)
                    expired.push_back({sub_id_p.first, sub_p.first, e.push_time, e.data});
            }
        }
        return expired;
    }

    /// \brief Reference a given subbuffer
    ///
    /// \throw goby::Exception If subbuffer doesn't exist
    DynamicSubBuffer<T>& sub(modem_id_type dest_id, const subbuffer_id_type& sub_id)
    {
        if (!sub_.count(dest_id) || !sub_.at(dest_id).count(sub_id))
            throw(goby::Exception("Subbuffer ID: " + sub_id +
                                  " does not exist, must call create(...) first."));
        return sub_.at(dest_id).at(sub_id);
    }

  private:
    // destination -> subbuffer id (group/type) -> subbuffer
    std::map<modem_id_type, std::unordered_map<subbuffer_id_type, DynamicSubBuffer<T> > > sub_;

    std::string glog_priority_group_;

}; // namespace acomms

} // namespace acomms
} // namespace goby
