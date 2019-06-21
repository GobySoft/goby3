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

#ifndef TransportCommon20160607H
#define TransportCommon20160607H

#include <chrono>
#include <memory>
#include <regex>
#include <thread>
#include <unordered_map>

#include "goby/exception.h"
#include "goby/util/binary.h"

#include "poller.h"
#include "transport-interfaces.h"

#include "goby/middleware/protobuf/interprocess_data.pb.h"

namespace goby
{
namespace middleware
{
// a do nothing transporter that is always inside the last real transporter level.
class NullTransporter : public StaticTransporterInterface<NullTransporter, NullTransporter>,
                        public Poller<NullTransporter>
{
  public:
    NullTransporter() = default;
    virtual ~NullTransporter() = default;

    template <typename Data> static constexpr int scheme()
    {
        return MarshallingScheme::NULL_SCHEME;
    }

    template <const Group& group> void check_validity() {}

    template <typename Data, int scheme = scheme<Data>()>
    void publish_dynamic(const Data& data, const Group& group,
                         const Publisher<Data>& publisher = Publisher<Data>())
    {
    }

    template <typename Data, int scheme = scheme<Data>()>
    void publish_dynamic(std::shared_ptr<Data> data, const Group& group,
                         const Publisher<Data>& publisher = Publisher<Data>())
    {
    }

    template <typename Data, int scheme = scheme<Data>()>
    void publish_dynamic(std::shared_ptr<const Data> data, const Group& group,
                         const Publisher<Data>& publisher = Publisher<Data>())
    {
    }

    template <typename Data, int scheme = scheme<Data>()>
    void subscribe_dynamic(std::function<void(const Data&)> f, const Group& group,
                           const Subscriber<Data>& subscriber = Subscriber<Data>())
    {
    }

    template <typename Data, int scheme = scheme<Data>()>
    void subscribe_dynamic(std::function<void(std::shared_ptr<const Data>)> f, const Group& group,
                           const Subscriber<Data>& subscriber = Subscriber<Data>())
    {
    }

    template <typename Data, int scheme = scheme<Data>()>
    void unsubscribe_dynamic(const Group& group)
    {
    }

  private:
    friend Poller<NullTransporter>;
    int _poll(std::unique_ptr<std::unique_lock<std::timed_mutex> >& lock) { return 0; }
};

class SerializationSubscriptionBase
{
  public:
    SerializationSubscriptionBase() = default;
    virtual ~SerializationSubscriptionBase() = default;

    virtual std::string::const_iterator post(std::string::const_iterator b,
                                             std::string::const_iterator e) const = 0;
    virtual std::vector<char>::const_iterator post(std::vector<char>::const_iterator b,
                                                   std::vector<char>::const_iterator e) const = 0;
    virtual const char* post(const char* b, const char* e) const = 0;
    virtual const std::string& type_name() const = 0;
    virtual const Group& subscribed_group() const = 0;

    virtual int scheme() const = 0;

    enum class SubscriptionAction
    {
        SUBSCRIBE,
        UNSUBSCRIBE
    };
    virtual SubscriptionAction action() const = 0;

    std::thread::id thread_id() const { return thread_id_; }

  private:
    const std::thread::id thread_id_{std::this_thread::get_id()};
};

inline bool operator==(const SerializationSubscriptionBase& s1,
                       const SerializationSubscriptionBase& s2)
{
    return s1.scheme() == s2.scheme() && s1.type_name() == s2.type_name() &&
           s1.subscribed_group() == s2.subscribed_group() && s1.action() == s2.action();
}

template <typename Data, int scheme_id>
class SerializationSubscription : public SerializationSubscriptionBase
{
  public:
    typedef std::function<void(std::shared_ptr<const Data> data,
                               const goby::middleware::protobuf::TransporterConfig& transport_cfg)>
        HandlerType;

    SerializationSubscription(HandlerType& handler, const Group& group,
                              const Subscriber<Data>& subscriber)
        : handler_(handler),
          type_name_(SerializerParserHelper<Data, scheme_id>::type_name()),
          group_(group),
          subscriber_(subscriber)
    {
    }

    // handle an incoming message
    std::string::const_iterator post(std::string::const_iterator b,
                                     std::string::const_iterator e) const override
    {
        return _post(b, e);
    }

    std::vector<char>::const_iterator post(std::vector<char>::const_iterator b,
                                           std::vector<char>::const_iterator e) const override
    {
        return _post(b, e);
    }

    const char* post(const char* b, const char* e) const override { return _post(b, e); }

    SerializationSubscriptionBase::SubscriptionAction action() const override
    {
        return SerializationSubscriptionBase::SubscriptionAction::SUBSCRIBE;
    }

    // getters
    const std::string& type_name() const override { return type_name_; }
    const Group& subscribed_group() const override { return group_; }
    int scheme() const override { return scheme_id; }

  private:
    template <typename CharIterator>
    CharIterator _post(CharIterator bytes_begin, CharIterator bytes_end) const
    {
        CharIterator actual_end;
        auto msg = std::make_shared<const Data>(
            SerializerParserHelper<Data, scheme_id>::parse(bytes_begin, bytes_end, actual_end));

        if (subscribed_group() == subscriber_.group(*msg))
            handler_(msg, goby::middleware::protobuf::TransporterConfig());
        return actual_end;
    }

  private:
    HandlerType handler_;
    const std::string type_name_;
    const Group group_;
    Subscriber<Data> subscriber_;
};

template <typename Data, int scheme_id>
class SerializationUnSubscription : public SerializationSubscriptionBase
{
  public:
    SerializationUnSubscription(const Group& group)
        : type_name_(SerializerParserHelper<Data, scheme_id>::type_name()), group_(group)
    {
    }

    std::string::const_iterator post(std::string::const_iterator b,
                                     std::string::const_iterator e) const override
    {
        throw(goby::Exception("Cannot call post on an UnSubscription"));
    }

    std::vector<char>::const_iterator post(std::vector<char>::const_iterator b,
                                           std::vector<char>::const_iterator e) const override
    {
        throw(goby::Exception("Cannot call post on an UnSubscription"));
    }

    const char* post(const char* b, const char* e) const override
    {
        throw(goby::Exception("Cannot call post on an UnSubscription"));
    }

    SerializationSubscriptionBase::SubscriptionAction action() const override
    {
        return SerializationSubscriptionBase::SubscriptionAction::UNSUBSCRIBE;
    }

    // getters
    const std::string& type_name() const override { return type_name_; }
    const Group& subscribed_group() const override { return group_; }
    int scheme() const override { return scheme_id; }

  private:
    const std::string type_name_;
    const Group group_;
};

class SerializationSubscriptionRegex
{
  public:
    typedef std::function<void(const std::vector<unsigned char>&, int scheme,
                               const std::string& type, const Group& group)>
        HandlerType;

    SerializationSubscriptionRegex(HandlerType& handler, const std::set<int>& schemes,
                                   const std::string& type_regex = ".*",
                                   const std::string& group_regex = ".*")
        : handler_(handler), schemes_(schemes), type_regex_(type_regex), group_regex_(group_regex)
    {
    }

    // handle an incoming message
    // return true if posted
    template <typename CharIterator>
    bool post(CharIterator bytes_begin, CharIterator bytes_end, int scheme, const std::string& type,
              const std::string& group) const
    {
        if ((schemes_.count(goby::middleware::MarshallingScheme::ALL_SCHEMES) ||
             schemes_.count(scheme)) &&
            std::regex_match(type, type_regex_) && std::regex_match(group, group_regex_))
        {
            std::vector<unsigned char> data(bytes_begin, bytes_end);
            handler_(data, scheme, type, goby::middleware::DynamicGroup(group));
            return true;
        }
        else
        {
            return false;
        }
    }

    std::thread::id thread_id() const { return thread_id_; }

  private:
    HandlerType handler_;
    const std::set<int> schemes_;
    std::regex type_regex_;
    std::regex group_regex_;
    const std::thread::id thread_id_{std::this_thread::get_id()};
};

class SerializationUnSubscribeAll
{
  public:
    std::thread::id thread_id() const { return thread_id_; }

  private:
    const std::thread::id thread_id_{std::this_thread::get_id()};
};

} // namespace middleware
} // namespace goby

namespace std
{
template <> struct hash<goby::middleware::Group>
{
    size_t operator()(const goby::middleware::Group& group) const noexcept
    {
        return std::hash<std::string>{}(std::string(group));
    }
};
} // namespace std

#endif
