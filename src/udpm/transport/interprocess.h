// Copyright 2016-2025:
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

#ifndef GOBY_UDPM_TRANSPORT_INTERPROCESS_H
#define GOBY_UDPM_TRANSPORT_INTERPROCESS_H

#include "goby/middleware/transport/interface.h"
#include "goby/middleware/transport/interprocess.h"

#include "goby/udpm/protobuf/interprocess_config.pb.h"

namespace goby
{
namespace middleware
{
template <typename Data> class Publisher;
} // namespace middleware

namespace udpm
{

constexpr char delimiter{'/'};
constexpr const char* delimiter_str{"/"};
// use old ASCII substitute char for '/' in group or type
constexpr char delimiter_substitute{0x1a};

enum class IdentifierWildcard
{
    NO_WILDCARDS,
    THREAD_WILDCARD,
    PROCESS_THREAD_WILDCARD
};

// scheme
inline std::string identifier_part_to_string(int i)
{
    return middleware::MarshallingScheme::to_string(i);
}
inline std::string identifier_part_to_string(std::thread::id i)
{
    return goby::middleware::thread_id(i);
}

/// Given key, find the string in the map, or create it (to_string) and store it, and return the string.
template <typename Key>
const std::string& id_component(const Key& k, std::unordered_map<Key, std::string>& map)
{
    auto it = map.find(k);
    if (it != map.end())
        return it->second;

    std::string v = identifier_part_to_string(k) + delimiter_str;
    auto it_pair = map.insert(std::make_pair(k, v));
    return it_pair.first->second;
}

inline std::string
make_identifier(const std::string& type_name, int scheme, const std::string& group,
                IdentifierWildcard wildcard, const std::string& process,
                std::unordered_map<int, std::string>* schemes_buffer = nullptr,
                std::unordered_map<std::thread::id, std::string>* threads_buffer = nullptr)
{
    // swap out delimiter with substitute
    std::string sanitized_type_name = type_name;
    std::replace(sanitized_type_name.begin(), sanitized_type_name.end(), delimiter,
                 delimiter_substitute);
    std::string sanitized_group_name = group;
    std::replace(sanitized_group_name.begin(), sanitized_group_name.end(), delimiter,
                 delimiter_substitute);
    switch (wildcard)
    {
        default:
        case IdentifierWildcard::NO_WILDCARDS:
        {
            auto thread = std::this_thread::get_id();
            return (
                delimiter_str + sanitized_group_name + delimiter_str +
                (schemes_buffer ? id_component(scheme, *schemes_buffer)
                                : std::string(identifier_part_to_string(scheme) + delimiter_str)) +
                sanitized_type_name + delimiter_str + process + delimiter_str +
                (threads_buffer ? id_component(thread, *threads_buffer)
                                : std::string(identifier_part_to_string(thread) + delimiter_str)));
        }
        case IdentifierWildcard::THREAD_WILDCARD:
        {
            return (delimiter_str + sanitized_group_name + delimiter_str +
                    (schemes_buffer
                         ? id_component(scheme, *schemes_buffer)
                         : std::string(identifier_part_to_string(scheme) + delimiter_str)) +
                    sanitized_type_name + delimiter_str + process + delimiter_str);
        }
        case IdentifierWildcard::PROCESS_THREAD_WILDCARD:
        {
            return (delimiter_str + sanitized_group_name + delimiter_str +
                    (schemes_buffer
                         ? id_component(scheme, *schemes_buffer)
                         : std::string(identifier_part_to_string(scheme) + delimiter_str)) +
                    sanitized_type_name + delimiter_str);
        }
    }
}

template <typename InnerTransporter,
          template <typename Derived, typename InnerTransporterType> class PortalBase>
class InterProcessPortalImplementation
    : public PortalBase<InterProcessPortalImplementation<InnerTransporter, PortalBase>,
                        InnerTransporter>
{
  public:
    using Base = PortalBase<InterProcessPortalImplementation<InnerTransporter, PortalBase>,
                            InnerTransporter>;

    InterProcessPortalImplementation(const protobuf::InterProcessPortalConfig& cfg) : cfg_(cfg)
    {
        _init();
    }

    InterProcessPortalImplementation(InnerTransporter& inner,
                                     const protobuf::InterProcessPortalConfig& cfg)
        : Base(inner), cfg_(cfg)
    {
        _init();
    }

    ~InterProcessPortalImplementation() {}

    // no-op - no hold implemented in UDPm
    void ready() {}

    friend Base;
    friend typename Base::Base;

  private:
    void _init() { goby::glog.set_lock_action(goby::util::logger_lock::lock); }

    template <typename Data, int scheme>
    void _publish(const Data& d, const goby::middleware::Group& group,
                  const middleware::Publisher<Data>& /*publisher*/, bool ignore_buffer = false)
    {
        std::vector<char> bytes(middleware::SerializerParserHelper<Data, scheme>::serialize(d));
        std::string type_name = middleware::SerializerParserHelper<Data, scheme>::type_name(d);
        _publish_serialized(type_name, scheme, bytes, group, ignore_buffer);
    }

    void _publish_serialized(std::string type_name, int scheme, const std::vector<char>& bytes,
                             const goby::middleware::Group& group, bool ignore_buffer = false)
    {
        std::string identifier = _make_fully_qualified_identifier(type_name, scheme, group) + '\0';
        //zmq_main_.publish(identifier, &bytes[0], bytes.size(), ignore_buffer);
    }

    template <typename Data, int scheme>
    void _subscribe(std::function<void(std::shared_ptr<const Data> d)> f,
                    const goby::middleware::Group& group,
                    const middleware::Subscriber<Data>& /*subscriber*/)
    {
        std::string identifier =
            _make_identifier<Data, scheme>(group, IdentifierWildcard::PROCESS_THREAD_WILDCARD);

        auto subscription = std::make_shared<middleware::SerializationSubscription<Data, scheme>>(
            f, group,
            middleware::Subscriber<Data>(goby::middleware::protobuf::TransporterConfig(),
                                         [=](const Data& /*d*/) { return group; }));

        portal_subscriptions_.insert(std::make_pair(identifier, subscription));
    }

    std::shared_ptr<middleware::SerializationSubscriptionRegex> _subscribe_regex(
        std::function<void(const std::vector<unsigned char>&, int scheme, const std::string& type,
                           const goby::middleware::Group& group)>
            f,
        const std::set<int>& schemes, const std::string& type_regex, const std::string& group_regex)
    {
        auto new_sub = std::make_shared<middleware::SerializationSubscriptionRegex>(
            f, schemes, type_regex, group_regex);
        _subscribe_regex(new_sub);
        return new_sub;
    }

    template <typename Data, int scheme>
    void _unsubscribe(
        const goby::middleware::Group& group,
        const middleware::Subscriber<Data>& /*subscriber*/ = middleware::Subscriber<Data>())
    {
        std::string identifier =
            _make_identifier<Data, scheme>(group, IdentifierWildcard::PROCESS_THREAD_WILDCARD);

        portal_subscriptions_.erase(identifier);
    }

    void _unsubscribe_all(
        const std::string& subscriber_id = identifier_part_to_string(std::this_thread::get_id()))
    {
        // portal unsubscribe
        if (subscriber_id == identifier_part_to_string(std::this_thread::get_id()))
        {
            portal_subscriptions_.clear();
        }
        else // forwarder unsubscribe
        {
            while (forwarder_subscription_identifiers_[subscriber_id].size() > 0)
                _forwarder_unsubscribe(
                    subscriber_id,
                    forwarder_subscription_identifiers_[subscriber_id].begin()->first);
        }

        // regex
        if (regex_subscriptions_.size() > 0)
        {
            regex_subscriptions_.erase(subscriber_id);
        }
    }

    int _poll(std::unique_ptr<std::unique_lock<std::timed_mutex>>& lock)
    {
        int items = 0;

        //++items;
        //if (lock)
        //              lock.reset();

        return items;
    }

    void _receive_publication_forwarded(
        const goby::middleware::protobuf::SerializerTransporterMessage& msg)
    {
        std::string identifier =
            _make_identifier(msg.key().type(), msg.key().marshalling_scheme(), msg.key().group(),
                             IdentifierWildcard::NO_WILDCARDS) +
            '\0';
        // auto& bytes = msg.data();

        //zmq_main_.publish(identifier, &bytes[0], bytes.size());
    }

    void _receive_subscription_forwarded(
        const std::shared_ptr<const middleware::SerializationHandlerBase<>>& subscription)
    {
        std::string identifier = _make_identifier(subscription->type_name(), subscription->scheme(),
                                                  subscription->subscribed_group(),
                                                  IdentifierWildcard::PROCESS_THREAD_WILDCARD);

        goby::glog.is_debug2() &&
            goby::glog << "Received subscription forwarded for identifier [" << identifier
                       << "] from subscriber id: " << subscription->subscriber_id() << std::endl;

        switch (subscription->action())
        {
            case middleware::SerializationHandlerBase<>::SubscriptionAction::SUBSCRIBE:
            {
                // insert if this thread hasn't already subscribed
                if (forwarder_subscription_identifiers_[subscription->subscriber_id()].count(
                        identifier) == 0)
                {
                    // first to subscribe from a Forwarder
                    if (forwarder_subscriptions_.count(identifier) == 0)
                    {
                        // create Forwarder subscription
                        forwarder_subscriptions_.insert(std::make_pair(identifier, subscription));
                    }
                    forwarder_subscription_identifiers_[subscription->subscriber_id()].insert(
                        std::make_pair(identifier, forwarder_subscriptions_.find(identifier)));
                }
            }
            break;

            case middleware::SerializationHandlerBase<>::SubscriptionAction::UNSUBSCRIBE:
            {
                _forwarder_unsubscribe(subscription->subscriber_id(), identifier);
            }
            break;

            default: break;
        }
    }

    void _forwarder_unsubscribe(const std::string& subscriber_id, const std::string& identifier)
    {
        auto it = forwarder_subscription_identifiers_[subscriber_id].find(identifier);
        if (it != forwarder_subscription_identifiers_[subscriber_id].end())
        {
            bool no_forwarder_subscribers = true;
            for (const auto& p : forwarder_subscription_identifiers_)
            {
                if (p.second.count(identifier) != 0)
                {
                    no_forwarder_subscribers = false;
                    break;
                }
            }

            // if no Forwarder subscriptions left
            if (no_forwarder_subscribers)
            {
                // erase the Forwarder subscription
                forwarder_subscriptions_.erase(it->second);
            }
            forwarder_subscription_identifiers_[subscriber_id].erase(it);
        }
    }

    void _receive_regex_subscription_forwarded(
        std::shared_ptr<const middleware::SerializationSubscriptionRegex> subscription)
    {
        _subscribe_regex(subscription);
    }

    void _subscribe_regex(
        const std::shared_ptr<const middleware::SerializationSubscriptionRegex>& new_sub)
    {
        regex_subscriptions_.insert(std::make_pair(new_sub->subscriber_id(), new_sub));
    }

    template <typename Data, int scheme>
    std::string _make_identifier(const goby::middleware::Group& group, IdentifierWildcard wildcard)
    {
        return _make_identifier(middleware::SerializerParserHelper<Data, scheme>::type_name(),
                                scheme, group, wildcard);
    }

    std::string _make_fully_qualified_identifier(const std::string& type_name, int scheme,
                                                 const std::string& group)
    {
        return _make_identifier(type_name, scheme, group, IdentifierWildcard::THREAD_WILDCARD) +
               id_component(std::this_thread::get_id(), threads_);
    }

    template <typename Data, int scheme>
    std::string _make_identifier(const Data& d, const goby::middleware::Group& group,
                                 IdentifierWildcard wildcard)
    {
        return _make_identifier(middleware::SerializerParserHelper<Data, scheme>::type_name(d),
                                scheme, group, wildcard);
    }

    std::string _make_identifier(const std::string& type_name, int scheme, const std::string& group,
                                 IdentifierWildcard wildcard)
    {
        return make_identifier(type_name, scheme, group, wildcard, process_, &schemes_, &threads_);
    }

    // group, scheme, type, process, thread
    std::tuple<std::string, int, std::string, int, std::size_t>
    parse_identifier(const std::string& identifier)
    {
        enum
        {
            POS_GROUP = 0,
            POS_SCHEME = 1,
            POS_TYPE = 2,
            POS_PROCESS = 3,
            POS_THREAD = 4,
            POS_MAX = POS_THREAD
        };

        const int number_elements = POS_MAX + 1;
        std::string::size_type previous_delimiter = 0;
        std::vector<std::string> elem;
        for (auto i = 0; i < number_elements; ++i)
        {
            auto delimiter_pos = identifier.find(delimiter, previous_delimiter + 1);
            elem.push_back(identifier.substr(previous_delimiter + 1,
                                             delimiter_pos - (previous_delimiter + 1)));
            previous_delimiter = delimiter_pos;
        }

        auto& group = elem[POS_GROUP];
        auto& type = elem[POS_TYPE];
        std::replace(type.begin(), type.end(), delimiter_substitute, delimiter);
        std::replace(group.begin(), group.end(), delimiter_substitute, delimiter);
        return std::make_tuple(elem[POS_GROUP],
                               middleware::MarshallingScheme::from_string(elem[POS_SCHEME]),
                               elem[POS_TYPE], std::stoi(elem[POS_PROCESS]),
                               std::stoull(elem[POS_THREAD], nullptr, 16));
    }

  private:
    const protobuf::InterProcessPortalConfig cfg_;

    // maps identifier to subscription
    std::unordered_multimap<std::string,
                            std::shared_ptr<const middleware::SerializationHandlerBase<>>>
        portal_subscriptions_;
    // only one subscription for each forwarded identifier
    std::unordered_map<std::string, std::shared_ptr<const middleware::SerializationHandlerBase<>>>
        forwarder_subscriptions_;
    std::unordered_map<
        std::string, std::unordered_map<
                         std::string, typename decltype(forwarder_subscriptions_)::const_iterator>>
        forwarder_subscription_identifiers_;

    std::unordered_multimap<std::string,
                            std::shared_ptr<const middleware::SerializationSubscriptionRegex>>
        regex_subscriptions_;
    std::string process_{std::to_string(getpid())};

    std::unordered_map<int, std::string> schemes_;
    std::unordered_map<std::thread::id, std::string> threads_;
};

template <typename InnerTransporter = middleware::NullTransporter>
using InterProcessPortal =
    InterProcessPortalImplementation<InnerTransporter, middleware::InterProcessPortalBase>;

} // namespace udpm
} // namespace goby

#endif
