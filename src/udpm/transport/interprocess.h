// Copyright 2026:
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

#include "goby/middleware/transport/identifier.h"
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

constexpr char identifier_end_delimiter{'\0'};

template <typename InnerTransporter,
          template <typename Derived, typename InnerTransporterType> class PortalBase>
class InterProcessPortalImplementation
    : public PortalBase<InterProcessPortalImplementation<InnerTransporter, PortalBase>,
                        InnerTransporter>
{
  public:
    using Base = PortalBase<InterProcessPortalImplementation<InnerTransporter, PortalBase>,
                            InnerTransporter>;

    using IdentifierWildcard = middleware::IdentifierWildcard;

    InterProcessPortalImplementation(const protobuf::InterProcessPortalConfig& cfg) : cfg_(cfg)
    {
        _init();
    }

    InterProcessPortalImplementation(InnerTransporter& inner,
                                     const protobuf::InterProcessPortalConfig& cfg)
        : InterProcessPortalImplementation(cfg)
    {
    }

    ~InterProcessPortalImplementation() {}

    // no-op - no hold implemented in UDPm
    void ready() {}
    bool hold_state() { return false; }

    friend Base;
    friend typename Base::Base;

  private:
    void _init() { goby::glog.set_lock_action(goby::util::logger_lock::lock); }

    void _publish_serialized(std::string type_name, int scheme, const std::vector<char>& bytes,
                             const goby::middleware::Group& group, bool ignore_buffer = false)
    {
        std::string identifier =
            this->_make_identifier(type_name, scheme, group, IdentifierWildcard::NO_WILDCARDS) +
            identifier_end_delimiter;
        //zmq_main_.publish(identifier, &bytes[0], bytes.size(), ignore_buffer);
    }

    template <typename Data, int scheme>
    void _subscribe(std::function<void(std::shared_ptr<const Data> d)> f,
                    const goby::middleware::Group& group,
                    const middleware::Subscriber<Data>& /*subscriber*/)
    {
        std::string identifier = this->template _make_identifier<Data, scheme>(
            group, IdentifierWildcard::PROCESS_THREAD_WILDCARD);

        auto subscription = std::make_shared<middleware::SerializationSubscription<Data, scheme>>(
            f, group,
            middleware::Subscriber<Data>(goby::middleware::protobuf::TransporterConfig(),
                                         [=](const Data& /*d*/) { return group; }));

        portal_subscriptions_.insert(std::make_pair(identifier, subscription));
    }

    template <typename Data, int scheme>
    void _unsubscribe(
        const goby::middleware::Group& group,
        const middleware::Subscriber<Data>& /*subscriber*/ = middleware::Subscriber<Data>())
    {
        std::string identifier = this->template _make_identifier<Data, scheme>(
            group, IdentifierWildcard::PROCESS_THREAD_WILDCARD);

        portal_subscriptions_.erase(identifier);
    }

    void _unsubscribe_all(const std::string& subscriber_id =
                              middleware::identifier_part_to_string(std::this_thread::get_id()))
    {
        // portal unsubscribe
        if (subscriber_id == middleware::identifier_part_to_string(std::this_thread::get_id()))
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

    void _receive_subscription_forwarded(
        const std::shared_ptr<const middleware::SerializationHandlerBase<>>& subscription)
    {
        std::string identifier = this->_make_identifier(
            subscription->type_name(), subscription->scheme(), subscription->subscribed_group(),
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

    void _subscribe_regex_serialized(
        const std::shared_ptr<const middleware::SerializationSubscriptionRegex>& new_sub)
    {
        regex_subscriptions_.insert(std::make_pair(new_sub->subscriber_id(), new_sub));
    }

  private:
    const protobuf::InterProcessPortalConfig cfg_;

    // portal_subscriptions_ and forwarder_subscriptions_: maps identifier to subscription
    std::unordered_multimap<std::string,
                            std::shared_ptr<const middleware::SerializationHandlerBase<>>>
        portal_subscriptions_;
    // only one subscription for each forwarded identifier
    std::unordered_map<std::string, std::shared_ptr<const middleware::SerializationHandlerBase<>>>
        forwarder_subscriptions_;

    // maps subscriber_id [thread id as string] to map of identifier to forwarder subscription
    std::unordered_map<
        std::string, std::unordered_map<
                         std::string, typename decltype(forwarder_subscriptions_)::const_iterator>>
        forwarder_subscription_identifiers_;

    // subscriber id to subscription
    std::unordered_multimap<std::string,
                            std::shared_ptr<const middleware::SerializationSubscriptionRegex>>
        regex_subscriptions_;
};

template <typename InnerTransporter = middleware::NullTransporter>
using InterProcessPortal =
    InterProcessPortalImplementation<InnerTransporter, middleware::InterProcessPortalBase>;

} // namespace udpm
} // namespace goby

#endif
