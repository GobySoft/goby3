// Copyright 2019-2020:
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

#ifndef TransportInterModule20201006H
#define TransportInterModule20201006H

#include <atomic>
#include <functional>
#include <sys/types.h>
#include <thread>
#include <tuple>
#include <unistd.h>

#include "goby/middleware/group.h"

#include "goby/middleware/protobuf/intermodule.pb.h"
#include "goby/middleware/transport/interprocess.h"
#include "goby/middleware/transport/null.h"
#include "goby/middleware/transport/poller.h"
#include "goby/middleware/transport/serialization_handlers.h"

namespace goby
{
namespace middleware
{
namespace protobuf
{
bool operator<(const SerializerTransporterKey& k1, const SerializerTransporterKey& k2)
{
    return k1.marshalling_scheme() != k2.marshalling_scheme()
               ? (k1.marshalling_scheme() < k2.marshalling_scheme())
               : (k1.type() != k2.type()
                      ? (k1.type() < k2.type())
                      : (k1.group() != k2.group() ? (k1.group() < k2.group()) : false));
}
} // namespace protobuf

template <typename Derived, typename InnerTransporter>
using InterModuleTransporterBase = InterProcessTransporterBase<Derived, InnerTransporter>;

/// \brief Implements the forwarder concept for the intermodule layer
///
/// The forwarder is intended to be used by inner nodes within the layer that do not connect directly to other nodes on that layer.
/// \tparam InnerTransporter The type of the inner transporter used to forward data to and from this node
template <typename InnerTransporter>
class InterModuleForwarder
    : public InterModuleTransporterBase<InterModuleForwarder<InnerTransporter>, InnerTransporter>
{
  public:
    using Base =
        InterModuleTransporterBase<InterModuleForwarder<InnerTransporter>, InnerTransporter>;

    /// \brief Construct a forwarder for the intermodule layer
    ///
    /// \param inner A reference to the inner transporter used to forward messages to and from the portal
    InterModuleForwarder(InnerTransporter& inner) : Base(inner)
    {
    }
    virtual ~InterModuleForwarder() { this->unsubscribe_all(); }

    friend Base;

  private:
    template <typename Data, int scheme>
    void _publish(const Data& d, const Group& group, const Publisher<Data>& publisher)
    {
        //create and forward publication to edge

        std::vector<char> bytes(SerializerParserHelper<Data, scheme>::serialize(d));
        std::string* sbytes = new std::string(bytes.begin(), bytes.end());
        goby::middleware::protobuf::SerializerTransporterMessage msg;
        auto* key = msg.mutable_key();

        key->set_marshalling_scheme(scheme);
        key->set_type(SerializerParserHelper<Data, scheme>::type_name(d));
        key->set_group(std::string(group));
        msg.set_allocated_data(sbytes);

        *key->mutable_cfg() = publisher.cfg();
        this->inner().template publish<Base::to_portal_group_>(msg);
    }

    template <typename Data, int scheme>
    void _subscribe(std::function<void(std::shared_ptr<const Data> d)> f, const Group& group,
                    const Subscriber<Data>& subscriber)
    {
        if (subscriptions_.empty())
            this->inner().template subscribe_dynamic<protobuf::SerializerTransporterMessage>(
                [this](const protobuf::SerializerTransporterMessage& msg) {
                    auto range = subscriptions_.equal_range(msg.key());
                    for (auto it = range.first; it != range.second; ++it)
                    {
                        it->second->post(msg.data().begin(), msg.data().end());
                    }
                },
                from_portal_group_);

        auto local_subscription = std::make_shared<SerializationSubscription<Data, scheme>>(
            f, group,
            middleware::Subscriber<Data>(goby::middleware::protobuf::TransporterConfig(),
                                         [=](const Data& d) { return group; }));

        using goby::middleware::intermodule::protobuf::Subscription;
        Subscription subscription;
        subscription.set_id(full_process_id());
        subscription.mutable_key()->set_marshalling_scheme(scheme);
        subscription.mutable_key()->set_type(SerializerParserHelper<Data, scheme>::type_name());
        subscription.mutable_key()->set_group(std::string(group));
        subscription.set_action(Subscription::SUBSCRIBE);

        this->inner().template publish<Base::to_portal_group_>(subscription);

        subscriptions_.insert(std::make_pair(subscription.key(), local_subscription));
    }

    template <typename Data, int scheme> void _unsubscribe(const Group& group)
    {
        using goby::middleware::intermodule::protobuf::Subscription;
        Subscription unsubscription;
        unsubscription.set_id(full_process_id());
        unsubscription.mutable_key()->set_marshalling_scheme(scheme);
        unsubscription.mutable_key()->set_type(SerializerParserHelper<Data, scheme>::type_name());
        unsubscription.mutable_key()->set_group(std::string(group));
        unsubscription.set_action(Subscription::UNSUBSCRIBE);
        this->inner().template publish<Base::to_portal_group_>(unsubscription);

        subscriptions_.erase(unsubscription.key());

        if (subscriptions_.empty())
            this->inner().template unsubscribe_dynamic<protobuf::SerializerTransporterMessage>(
                from_portal_group_);
    }

    void _unsubscribe_all()
    {
        using goby::middleware::intermodule::protobuf::Subscription;
        Subscription unsubscription;
        unsubscription.set_id(full_process_id());
        unsubscription.set_action(Subscription::UNSUBSCRIBE_ALL);
        this->inner().template publish<Base::to_portal_group_>(unsubscription);

        subscriptions_.clear();
        this->inner().template unsubscribe_dynamic<protobuf::SerializerTransporterMessage>(
            from_portal_group_);
    }

    // not yet implemented
    // void _subscribe_regex(std::function<void(const std::vector<unsigned char>&, int scheme,
    //                                          const std::string& type, const Group& group)>
    //                           f,
    //                       const std::set<int>& schemes, const std::string& type_regex = ".*",
    //                       const std::string& group_regex = ".*")
    // {
    // }

    int _poll(std::unique_ptr<std::unique_lock<std::timed_mutex>>& lock)
    {
        return 0;
    } // A forwarder is a shell, only the inner Transporter has data

  private:
    std::multimap<protobuf::SerializerTransporterKey,
                  std::shared_ptr<const middleware::SerializationHandlerBase<>>>
        subscriptions_;

    DynamicGroup from_portal_group_{Base::from_portal_group_prefix_ + full_process_id()};
};

template <typename Derived, typename InnerTransporter>
class InterModulePortalBase : public InterModuleTransporterBase<Derived, InnerTransporter>
{
  public:
    using Base = InterModuleTransporterBase<Derived, InnerTransporter>;

    InterModulePortalBase(InnerTransporter& inner) : Base(inner) { _init(); }
    InterModulePortalBase() { _init(); }

    virtual ~InterModulePortalBase() {}

  private:
    void _init()
    {
        using goby::middleware::intermodule::protobuf::Subscription;
        using goby::middleware::protobuf::SerializerTransporterMessage;
        this->inner().template subscribe<Base::to_portal_group_, SerializerTransporterMessage>(
            [this](const SerializerTransporterMessage& d) {
                static_cast<Derived*>(this)->_receive_publication_forwarded(d);
            });

        this->inner().template subscribe<Base::to_portal_group_, Subscription>(
            [this](const Subscription& s) {
                std::string group_name(Base::from_portal_group_prefix_ + s.id());
                auto on_subscribe = [this, group_name](const SerializerTransporterMessage& d) {
                    DynamicGroup group(group_name);
                    this->inner().publish_dynamic(d, group);
                };
                auto sub = std::make_shared<SerializationInterModuleSubscription>(on_subscribe, s);

                switch (s.action())
                {
                    case Subscription::SUBSCRIBE:
                    case Subscription::UNSUBSCRIBE:
                        static_cast<Derived*>(this)->_receive_subscription_forwarded(sub);
                        break;
                    case Subscription::UNSUBSCRIBE_ALL:
                        static_cast<Derived*>(this)->_unsubscribe_all(s.id());
                        break;
                }
            });
    }
};

} // namespace middleware
} // namespace goby

#endif
