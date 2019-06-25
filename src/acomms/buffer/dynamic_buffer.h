// Copyright 2009-2018 Toby Schneider (http://gobysoft.org/index.wt/people/toby)//                     GobySoft, LLC (2013-)
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

#include "goby/acomms/protobuf/buffer.pb.h"
#include "goby/exception.h"
#include "goby/time/convert.h"
#include "goby/time/steady_clock.h"
#include "goby/util/debug_logger.h"

namespace goby
{
namespace acomms
{
/// Represents a time-dependent priority queue for a single group of messages (e.g. for a single DCCL ID)
template <typename T> class DynamicSubBuffer
{
  public:
    using size_type = typename std::deque<T>::size_type;
    using full_value_type = std::pair<goby::time::SteadyClock::time_point, T>;

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

    /// \brief Returns the value at the top of the queue
    full_value_type& top()
    {
        last_access_ = goby::time::SteadyClock::now();
        return c_.front();
    }

    /// \brief Provides the numerical priority value based on this subbuffer's base priority, time-to-live (ttl) and time since last access (last call to top())
    ///
    /// \param reference time point to use for current reference when calculating this priority value (defaults to current time)
    /// \return priority value for this sub buffer
    double
    top_value(goby::time::SteadyClock::time_point reference = goby::time::SteadyClock::now()) const
    {
        if (empty())
            return -std::numeric_limits<double>::infinity();

        using Duration = std::chrono::microseconds;

        double dt = std::chrono::duration_cast<Duration>(reference - last_access_).count();
        double ttl = goby::time::convert_duration<Duration>(cfg_.ttl_with_units()).count();
        double v_b = cfg_.value_base();

        return v_b * dt / ttl;
    }

    /// \brief Returns if this queue is empty
    bool empty() const { return c_.empty(); }

    /// \brief Retrieves the size of the queue
    size_type size() const { return c_.size(); }

    /// \brief Pop the value on the top of the queue
    void pop() { c_.pop_front(); }

    /// \brief Push a value to the queue
    ///
    /// \param t Value to push
    /// \param reference Reference time to use for this value (defaults to current time)
    /// \return vector of values removed due to max_queue being exceeded
    std::vector<full_value_type>
    push(const T& t, goby::time::SteadyClock::time_point reference = goby::time::SteadyClock::now())
    {
        std::vector<full_value_type> exceeded;

        if (cfg_.newest_first())
            c_.push_front(std::make_pair(reference, t));
        else
            c_.push_back(std::make_pair(reference, t));

        if (c_.size() > cfg_.max_queue())
        {
            exceeded.push_back(c_.back());
            c_.pop_back();
        }
        return exceeded;
    }

    /// \brief Erase any values that have exceeded their time-to-live
    ///
    /// \return Vector of values that have expired and have been erased
    std::vector<full_value_type> expire()
    {
        std::vector<full_value_type> expired;

        auto now = goby::time::SteadyClock::now();
        auto ttl =
            goby::time::convert_duration<goby::time::SteadyClock::duration>(cfg_.ttl_with_units());
        if (cfg_.newest_first())
        {
            while (!c_.empty() && now > (c_.back().first + ttl))
            {
                expired.push_back(c_.back());
                c_.pop_back();
            }
        }
        else
        {
            while (!c_.empty() && now > (c_.front().first + ttl))
            {
                expired.push_back(c_.front());
                c_.pop_front();
            }
        }
        return expired;
    }

    /// \brief Erase a value
    ///
    /// \param value Value to erase (if it exists)
    /// \return true if the value was found and erase, false if the value was not found
    bool erase(const full_value_type& value)
    {
        // start at the beginning as we are most likely to want to erase elements we recently asked for with top()

        for (auto it = c_.begin(), end = c_.end(); it != end; ++it)
        {
            if (*it == value)
            {
                c_.erase(it);
                return true;
            }

            // if these are true, we're not going to find it so stop looking
            if (cfg_.newest_first() && it->first < value.first)
                break;
            else if (!cfg_.newest_first() && it->first > value.first)
                break;
        }
        return false;
    }

  private:
    goby::acomms::protobuf::DynamicBufferConfig cfg_;
    typename std::deque<full_value_type> c_;
    goby::time::SteadyClock::time_point last_access_{goby::time::SteadyClock::now()};
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
    using full_value_type = std::tuple<subbuffer_id_type, goby::time::SteadyClock::time_point, T>;

    /// \brief Create a new subbuffer with the given configuration
    ///
    /// This must be called before using functions that reference this subbuffer ID (e.g. push(...), erase(...))
    /// \param sub_id An identifier for this subbuffer
    /// \param cfg The configuration for this new subbuffer
    void create(const subbuffer_id_type& sub_id,
                const goby::acomms::protobuf::DynamicBufferConfig& cfg)
    {
        create(sub_id, std::vector<goby::acomms::protobuf::DynamicBufferConfig>(1, cfg));
    }

    /// \brief Create a new subbuffer merging the given configuration (See DynamicSubBuffer() for details)
    ///
    /// This must be called before using functions that reference this subbuffer ID (e.g. push(...), erase(...))
    /// \param sub_id An identifier for this subbuffer
    /// \param cfgs The configuration for this new subbuffer
    void create(const subbuffer_id_type& sub_id,
                const std::vector<goby::acomms::protobuf::DynamicBufferConfig>& cfgs)
    {
        if (sub_.count(sub_id))
            throw(goby::Exception("Subbuffer ID: " + sub_id + " already exists."));

        sub_.insert(std::make_pair(sub_id, DynamicSubBuffer<T>(cfgs)));
    }

    /// \brief Push a new message to the buffer
    ///
    /// \value fvt Full tuple giving subbuffer id, time, and value
    /// \return vector of values removed due to max_queue being exceeded
    /// \throw goby::Exception If subbuffer doesn't exist
    std::vector<full_value_type> push(const full_value_type& fvt)
    {
        std::vector<full_value_type> exceeded;
        auto sub_exceeded = sub(std::get<0>(fvt)).push(std::get<2>(fvt), std::get<1>(fvt));
        for (const auto& e : sub_exceeded)
            exceeded.push_back(std::make_tuple(std::get<0>(fvt), e.first, e.second));
        return exceeded;
    }

    /// \brief Push a new message to the buffer (overload allowing for time to be automatically calculated)
    ///
    /// \param sub_id The identifier for the subbuffer for which these data should be pushed
    /// \param t the data to push
    /// \return vector of values removed due to max_queue being exceeded
    /// \param tp the time to record for pushing the data (defaults to the current time)
    std::vector<full_value_type>
    push(const subbuffer_id_type& sub_id, const T& t,
         const goby::time::SteadyClock::time_point& tp = goby::time::SteadyClock::now())
    {
        return push(std::make_tuple(sub_id, tp, t));
    }

    /// \brief Is this buffer empty (that is, are all subbuffers empty)?
    bool empty() const
    {
        for (const auto& sub_p : sub_)
        {
            if (!sub_p.second.empty())
                return false;
        }
        return true;
    }

    /// \brief Size of the buffer (that is, sum of the subbuffer sizes)
    size_type size() const
    {
        size_type size = 0;
        for (const auto& sub_p : sub_) size += sub_p.second.size();
        return size;
    }

    /// \brief Returns the top value in a priority contest between all subbuffers
    ///
    /// \return Value with the highest priority (DynamicSubBuffer::top_value()) of all the subbuffers
    full_value_type top()
    {
        using goby::glog;

        glog.is_debug1() && glog << group(glog_priority_group_)
                                 << "Starting priority contest:" << std::endl;

        auto last_winning_sub_ = sub_.begin();
        double winning_value = 0;

        auto now = goby::time::SteadyClock::now();
        for (auto it = sub_.begin(), end = sub_.end(); it != end; ++it)
        {
            double value = it->second.top_value(now);
            glog.is_debug1() && glog << group(glog_priority_group_) << "\t" << it->first << "["
                                     << it->second.size() << "]: " << value << std::endl;

            if (value > winning_value)
            {
                winning_value = value;
                last_winning_sub_ = it;
            }
        }

        glog.is_debug1() && glog << group(glog_priority_group_)
                                 << "Winner: " << last_winning_sub_->first << std::endl;

        const auto& top_p = last_winning_sub_->second.top();
        return std::make_tuple(last_winning_sub_->first, top_p.first, top_p.second);
    }

    /// \brief Erase a value
    ///
    /// \param value Value to erase (if it exists)
    /// \return true if the value was found and erase, false if the value was not found
    /// \throw goby::Exception If subbuffer doesn't exist
    bool erase(const full_value_type& value)
    {
        return sub(std::get<0>(value))
            .erase(std::make_pair(std::get<1>(value), std::get<2>(value)));
    }

    /// \brief Erase any values that have exceeded their time-to-live
    ///
    /// \return Vector of values that have expired and have been erased
    std::vector<full_value_type> expire()
    {
        std::vector<full_value_type> expired;
        for (auto& sub_p : sub_)
        {
            auto sub_expired = sub_p.second.expire();
            for (const auto& e : sub_expired)
                expired.push_back(std::make_tuple(sub_p.first, e.first, e.second));
        }
        return expired;
    }

    /// \brief Reference a given subbuffer
    ///
    /// \throw goby::Exception If subbuffer doesn't exist
    DynamicSubBuffer<T>& sub(const subbuffer_id_type& sub_id)
    {
        if (!sub_.count(sub_id))
            throw(goby::Exception("Subbuffer ID: " + sub_id +
                                  " does not exist, must call create(...) first."));
        return sub_.at(sub_id);
    }

  private:
    std::map<subbuffer_id_type, DynamicSubBuffer<T> > sub_;
    typename decltype(sub_)::iterator last_winning_sub_{sub_.end()};

    std::string glog_priority_group_;

}; // namespace acomms

} // namespace acomms
} // namespace goby
