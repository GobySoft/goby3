// Copyright 2016-2023:
//   GobySoft, LLC (2013-)
//   Community contributors (see AUTHORS file)
// File authors:
//   Toby Schneider <toby@gobysoft.org>
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

#ifndef GOBY_MIDDLEWARE_TRANSPORT_SERIALIZATION_HANDLERS_H
#define GOBY_MIDDLEWARE_TRANSPORT_SERIALIZATION_HANDLERS_H

#include <chrono>
#include <memory>
#include <regex>
#include <thread>
#include <unordered_map>

#include "goby/exception.h"
#include "goby/util/binary.h"

#include "goby/middleware/common.h"
#include "goby/middleware/protobuf/intermodule.pb.h"
#include "goby/middleware/protobuf/intervehicle.pb.h"
#include "goby/middleware/protobuf/serializer_transporter.pb.h"

#include "interface.h"
#include "null.h"

namespace goby
{
namespace middleware
{
/// \brief Selector class for enabling SerializationHandlerBase::post() override signature based on whether the Metadata exists (e.g. Publisher or Subscriber) or not (that is, Metadata = void).
template <typename Metadata, typename Enable = void> class SerializationHandlerPostSelector
{
};

/// \brief Selects the SerializationHandlerBase::post() signatures without metadata
template <typename Metadata>
class SerializationHandlerPostSelector<Metadata,
                                       typename std::enable_if_t<std::is_void<Metadata>::value>>
{
  public:
    SerializationHandlerPostSelector() = default;
    virtual ~SerializationHandlerPostSelector() = default;

    virtual std::string::const_iterator post(std::string::const_iterator b,
                                             std::string::const_iterator e) const = 0;
#ifndef _LIBCPP_VERSION
    virtual std::vector<char>::const_iterator post(std::vector<char>::const_iterator b,
                                                   std::vector<char>::const_iterator e) const = 0;
#endif
    virtual const char* post(const char* b, const char* e) const = 0;
};

/// \brief Selects the SerializationHandlerBase::post() signatures with metadata (e.g. Publisher or Subscriber)
template <typename Metadata>
class SerializationHandlerPostSelector<Metadata,
                                       typename std::enable_if_t<!std::is_void<Metadata>::value>>
{
  public:
    SerializationHandlerPostSelector() = default;
    virtual ~SerializationHandlerPostSelector() = default;

    virtual std::string::const_iterator post(std::string::const_iterator b,
                                             std::string::const_iterator e,
                                             const Metadata& metadata) const = 0;
#ifndef _LIBCPP_VERSION
    virtual std::vector<char>::const_iterator post(std::vector<char>::const_iterator b,
                                                   std::vector<char>::const_iterator e,
                                                   const Metadata& metadata) const = 0;
#endif
    virtual const char* post(const char* b, const char* e, const Metadata& metadata) const = 0;
};

/// \brief Base class for handling posting callbacks for serialized data types (interprocess and outer)
///
/// \tparam Metadata metadata type (e.g. Publisher or Subscriber)
template <typename Metadata = void>
class SerializationHandlerBase : public SerializationHandlerPostSelector<Metadata>
{
  public:
    SerializationHandlerBase() = default;
    virtual ~SerializationHandlerBase() = default;

    virtual const std::string& type_name() const = 0;
    virtual const Group& subscribed_group() const = 0;

    virtual int scheme() const = 0;

    enum class SubscriptionAction
    {
        SUBSCRIBE,
        UNSUBSCRIBE,
        PUBLISHER_CALLBACK
    };
    virtual SubscriptionAction action() const = 0;

    std::thread::id thread_id() const { return thread_id_; }
    virtual std::string subscriber_id() const { return subscriber_id_; }

  private:
    const std::thread::id thread_id_{std::this_thread::get_id()};
    const std::string subscriber_id_{goby::middleware::thread_id(thread_id_)};
};

template <typename Metadata>
bool operator==(const SerializationHandlerBase<Metadata>& s1,
                const SerializationHandlerBase<Metadata>& s2)
{
    return s1.scheme() == s2.scheme() && s1.type_name() == s2.type_name() &&
           s1.subscribed_group() == s2.subscribed_group() && s1.action() == s2.action();
}

/// \brief Represents a subscription to a serialized data type (interprocess layer).
///
/// \tparam Data Subscribed data type
/// \tparam scheme_id Marshalling scheme id (typically MarshallingScheme::MarshallingSchemeEnum).
template <typename Data, int scheme_id>
class SerializationSubscription : public SerializationHandlerBase<>
{
  public:
    typedef std::function<void(std::shared_ptr<const Data> data)> HandlerType;

    SerializationSubscription(HandlerType handler,
                              const Group& group = Group(Group::broadcast_group),
                              const Subscriber<Data>& subscriber = Subscriber<Data>())
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

#ifndef _LIBCPP_VERSION
    std::vector<char>::const_iterator post(std::vector<char>::const_iterator b,
                                           std::vector<char>::const_iterator e) const override
    {
        return _post(b, e);
    }
#endif

    const char* post(const char* b, const char* e) const override { return _post(b, e); }

    SerializationHandlerBase<>::SubscriptionAction action() const override
    {
        return SerializationHandlerBase<>::SubscriptionAction::SUBSCRIBE;
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
        auto msg = SerializerParserHelper<Data, scheme_id>::parse(bytes_begin, bytes_end,
                                                                  actual_end, type_name_);

        if (subscribed_group() == subscriber_.group(*msg) && handler_)
            handler_(msg);

        return actual_end;
    }

  private:
    HandlerType handler_;
    const std::string type_name_;
    const Group group_;
    Subscriber<Data> subscriber_;
};

/// \brief Represents a subscription to a serialized data type (intervehicle layer).
///
/// \tparam Data Subscribed data type
/// \tparam scheme_id Marshalling scheme id (typically MarshallingScheme::MarshallingSchemeEnum).
template <typename Data, int scheme_id>
class IntervehicleSerializationSubscription
    : public SerializationHandlerBase<intervehicle::protobuf::Header>
{
  public:
    typedef std::function<void(std::shared_ptr<const Data> data)> HandlerType;

    IntervehicleSerializationSubscription(HandlerType handler,
                                          const Group& group = Group(Group::broadcast_group),
                                          const Subscriber<Data>& subscriber = Subscriber<Data>())
        : handler_(handler),
          type_name_(SerializerParserHelper<Data, scheme_id>::type_name()),
          group_(group),
          subscriber_(subscriber)
    {
    }

    // handle an incoming message
    std::string::const_iterator post(std::string::const_iterator b, std::string::const_iterator e,
                                     const intervehicle::protobuf::Header& header) const override
    {
        return _post(b, e, header);
    }

#ifndef _LIBCPP_VERSION
    std::vector<char>::const_iterator
    post(std::vector<char>::const_iterator b, std::vector<char>::const_iterator e,
         const intervehicle::protobuf::Header& header) const override
    {
        return _post(b, e, header);
    }
#endif

    const char* post(const char* b, const char* e,
                     const intervehicle::protobuf::Header& header) const override
    {
        return _post(b, e, header);
    }

    SerializationHandlerBase<intervehicle::protobuf::Header>::SubscriptionAction
    action() const override
    {
        return SerializationHandlerBase<
            intervehicle::protobuf::Header>::SubscriptionAction::SUBSCRIBE;
    }

    // getters
    const std::string& type_name() const override { return type_name_; }
    const Group& subscribed_group() const override { return group_; }
    int scheme() const override { return scheme_id; }

  private:
    template <typename CharIterator>
    CharIterator _post(CharIterator bytes_begin, CharIterator bytes_end,
                       const intervehicle::protobuf::Header& header) const
    {
        CharIterator actual_end;
        auto msg = SerializerParserHelper<Data, scheme_id>::parse(bytes_begin, bytes_end,
                                                                  actual_end, type_name_);

        subscriber_.set_link_data(*msg, header);

        if (subscribed_group() == subscriber_.group(*msg) && handler_)
            handler_(msg);

        return actual_end;
    }

  private:
    HandlerType handler_;
    const std::string type_name_;
    const Group group_;
    Subscriber<Data> subscriber_;
};

/// \brief Represents a callback for a published data type (e.g. acked_func or expired_func)
template <typename Data, int scheme_id, typename Metadata>
class PublisherCallback : public SerializationHandlerBase<Metadata>
{
  public:
    typedef std::function<void(const Data& data, const Metadata& md)> HandlerType;

    PublisherCallback(HandlerType handler)
        : handler_(handler), type_name_(SerializerParserHelper<Data, scheme_id>::type_name())
    {
    }

    PublisherCallback(HandlerType handler, const Data& data)
        : handler_(handler), type_name_(SerializerParserHelper<Data, scheme_id>::type_name(data))
    {
    }

    // handle an incoming message
    std::string::const_iterator post(std::string::const_iterator b, std::string::const_iterator e,
                                     const Metadata& md) const override
    {
        return _post(b, e, md);
    }

#ifndef _LIBCPP_VERSION
    std::vector<char>::const_iterator post(std::vector<char>::const_iterator b,
                                           std::vector<char>::const_iterator e,
                                           const Metadata& md) const override
    {
        return _post(b, e, md);
    }
#endif

    const char* post(const char* b, const char* e, const Metadata& md) const override
    {
        return _post(b, e, md);
    }

    typename SerializationHandlerBase<Metadata>::SubscriptionAction action() const override
    {
        return SerializationHandlerBase<Metadata>::SubscriptionAction::PUBLISHER_CALLBACK;
    }

    // getters
    const std::string& type_name() const override { return type_name_; }
    const Group& subscribed_group() const override { return group_; }
    int scheme() const override { return scheme_id; }

  private:
    template <typename CharIterator>
    CharIterator _post(CharIterator bytes_begin, CharIterator bytes_end, const Metadata& md) const
    {
        CharIterator actual_end;
        auto msg = SerializerParserHelper<Data, scheme_id>::parse(bytes_begin, bytes_end,
                                                                  actual_end, type_name_);

        if (handler_)
            handler_(*msg, md);
        return actual_end;
    }

  private:
    HandlerType handler_;
    const std::string type_name_;
    Group group_{Group(Group::broadcast_group)};
};

/// \brief Represents an unsubscription to a serialized data type (interprocess and outer layers).
///
/// \tparam Data Subscribed data type
/// \tparam scheme_id Marshalling scheme id (typically MarshallingScheme::MarshallingSchemeEnum).
template <typename Data, int scheme_id>
class SerializationUnSubscription : public SerializationHandlerBase<>
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

#ifndef _LIBCPP_VERSION
    std::vector<char>::const_iterator post(std::vector<char>::const_iterator b,
                                           std::vector<char>::const_iterator e) const override
    {
        throw(goby::Exception("Cannot call post on an UnSubscription"));
    }
#endif

    const char* post(const char* b, const char* e) const override
    {
        throw(goby::Exception("Cannot call post on an UnSubscription"));
    }

    SerializationHandlerBase<>::SubscriptionAction action() const override
    {
        return SerializationHandlerBase<>::SubscriptionAction::UNSUBSCRIBE;
    }

    // getters
    const std::string& type_name() const override { return type_name_; }
    const Group& subscribed_group() const override { return group_; }
    int scheme() const override { return scheme_id; }

  private:
    const std::string type_name_;
    const Group group_;
};

/// \brief Represents a regex subscription to a serialized data type (interprocess and outer layers).
class SerializationSubscriptionRegex
{
  public:
    typedef std::function<void(const std::vector<unsigned char>&, int scheme,
                               const std::string& type, const Group& group)>
        HandlerType;

    SerializationSubscriptionRegex(HandlerType handler, const std::set<int>& schemes,
                                   const std::string& type_regex = ".*",
                                   const std::string& group_regex = ".*")
        : handler_(handler), schemes_(schemes), type_regex_(type_regex), group_regex_(group_regex)
    {
    }

    void update_type_regex(const std::string& type_regex) { type_regex_.assign(type_regex); }
    void update_group_regex(const std::string& group_regex) { group_regex_.assign(group_regex); }

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
    std::string subscriber_id() const { return subscriber_id_; }

  private:
    HandlerType handler_;
    const std::set<int> schemes_;
    std::regex type_regex_;
    std::regex group_regex_;
    const std::thread::id thread_id_{std::this_thread::get_id()};
    const std::string subscriber_id_{goby::middleware::thread_id(thread_id_)};
};

/// \brief Represents an unsubscription to all subscribed data for a given thread
class SerializationUnSubscribeAll
{
  public:
    std::thread::id thread_id() const { return thread_id_; }
    std::string subscriber_id() const { return subscriber_id_; }

  private:
    const std::thread::id thread_id_{std::this_thread::get_id()};
    const std::string subscriber_id_{goby::middleware::thread_id(thread_id_)};
};

/// \brief Represents a(n) (un)subscription from an InterModuleForwarder
class SerializationInterModuleSubscription : public SerializationHandlerBase<>
{
  public:
    typedef std::function<void(const protobuf::SerializerTransporterMessage& d)> HandlerType;

    SerializationInterModuleSubscription(HandlerType handler,
                                         const intermodule::protobuf::Subscription sub)
        : handler_(handler), sub_cfg_(sub), group_(sub_cfg_.key().group()), subscriber_id_(sub.id())
    {
    }

    // handle an incoming message
    std::string::const_iterator post(std::string::const_iterator b,
                                     std::string::const_iterator e) const override
    {
        return _post(b, e);
    }

#ifndef _LIBCPP_VERSION
    std::vector<char>::const_iterator post(std::vector<char>::const_iterator b,
                                           std::vector<char>::const_iterator e) const override
    {
        return _post(b, e);
    }
#endif

    const char* post(const char* b, const char* e) const override { return _post(b, e); }

    SerializationHandlerBase<>::SubscriptionAction action() const override
    {
        switch (sub_cfg_.action())
        {
            default:
            case intermodule::protobuf::Subscription::SUBSCRIBE:
                return SerializationHandlerBase<>::SubscriptionAction::SUBSCRIBE;
            case intermodule::protobuf::Subscription::UNSUBSCRIBE:
            case intermodule::protobuf::Subscription::UNSUBSCRIBE_ALL:
                return SerializationHandlerBase<>::SubscriptionAction::UNSUBSCRIBE;
        }
    }

    // getters
    const std::string& type_name() const override { return sub_cfg_.key().type(); }
    const Group& subscribed_group() const override { return group_; }
    int scheme() const override { return sub_cfg_.key().marshalling_scheme(); }

    std::string subscriber_id() const override { return subscriber_id_; }

  private:
    template <typename CharIterator>
    CharIterator _post(CharIterator bytes_begin, CharIterator bytes_end) const
    {
        protobuf::SerializerTransporterMessage msg;
        std::string* sbytes = new std::string(bytes_begin, bytes_end);
        *msg.mutable_key() = sub_cfg_.key();
        msg.set_allocated_data(sbytes);
        handler_(msg);

        CharIterator actual_end = bytes_end;
        return actual_end;
    }

  private:
    HandlerType handler_;
    intermodule::protobuf::Subscription sub_cfg_;
    DynamicGroup group_;
    const std::thread::id thread_id_;
    const std::string subscriber_id_;
};

} // namespace middleware
} // namespace goby

#endif
