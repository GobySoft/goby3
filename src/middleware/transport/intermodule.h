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

/// \brief Base class for implementing transporters (both portal and forwarder) for the interprocess layer
///
/// \tparam Derived derived class (curiously recurring template pattern)
/// \tparam InnerTransporter inner layer transporter type
template <typename Derived, typename InnerTransporter>
class InterModuleTransporterBase
    : public StaticTransporterInterface<InterModuleTransporterBase<Derived, InnerTransporter>,
                                        InnerTransporter>,
      public Poller<InterModuleTransporterBase<Derived, InnerTransporter>>
{
    using InterfaceType =
        StaticTransporterInterface<InterModuleTransporterBase<Derived, InnerTransporter>,
                                   InnerTransporter>;

    using PollerType = Poller<InterModuleTransporterBase<Derived, InnerTransporter>>;

  public:
    InterModuleTransporterBase(InnerTransporter& inner)
        : InterfaceType(inner), PollerType(&this->inner())
    {
    }
    InterModuleTransporterBase() : PollerType(&this->inner()) {}

    virtual ~InterModuleTransporterBase() {}

    /// \brief Publish a message using a run-time defined DynamicGroup (const reference variant). Where possible, prefer the static variant in StaticTransporterInterface::publish()
    ///
    /// \tparam Data data type to publish. Can usually be inferred from the \c data parameter.
    /// \tparam scheme Marshalling scheme id (typically MarshallingScheme::MarshallingSchemeEnum). Can usually be inferred from the Data type.
    /// \param data Message to publish
    /// \param group group to publish this message to (typically a DynamicGroup)
    /// \param publisher Optional metadata that controls the publication or sets callbacks to monitor the result. Typically unnecessary for interprocess and inner layers.
    template <typename Data, int scheme = scheme<Data>()>
    void publish_dynamic(const Data& data, const Group& group,
                         const Publisher<Data>& publisher = Publisher<Data>())
    {
        check_validity_runtime(group);
        static_cast<Derived*>(this)->template _publish<Data, scheme>(data, group, publisher);
        this->inner().template publish_dynamic<Data, scheme>(data, group, publisher);
    }

    /// \brief Publish a message using a run-time defined DynamicGroup (shared pointer to const data variant). Where possible, prefer the static variant in StaticTransporterInterface::publish()
    ///
    /// \tparam Data data type to publish. Can usually be inferred from the \c data parameter.
    /// \tparam scheme Marshalling scheme id (typically MarshallingScheme::MarshallingSchemeEnum). Can usually be inferred from the Data type.
    /// \param data Message to publish
    /// \param group group to publish this message to (typically a DynamicGroup)
    /// \param publisher Optional metadata that controls the publication or sets callbacks to monitor the result. Typically unnecessary for interprocess and inner layers.
    template <typename Data, int scheme = scheme<Data>()>
    void publish_dynamic(std::shared_ptr<const Data> data, const Group& group,
                         const Publisher<Data>& publisher = Publisher<Data>())
    {
        if (data)
        {
            check_validity_runtime(group);
            static_cast<Derived*>(this)->template _publish<Data, scheme>(*data, group, publisher);
            this->inner().template publish_dynamic<Data, scheme>(data, group, publisher);
        }
    }

    /// \brief Publish a message using a run-time defined DynamicGroup (shared pointer to mutable data variant). Where possible, prefer the static variant in StaticTransporterInterface::publish()
    ///
    /// \tparam Data data type to publish. Can usually be inferred from the \c data parameter.
    /// \tparam scheme Marshalling scheme id (typically MarshallingScheme::MarshallingSchemeEnum). Can usually be inferred from the Data type.
    /// \param data Message to publish
    /// \param group group to publish this message to (typically a DynamicGroup)
    /// \param publisher Optional metadata that controls the publication or sets callbacks to monitor the result. Typically unnecessary for interprocess and inner layers.
    template <typename Data, int scheme = scheme<Data>()>
    void publish_dynamic(std::shared_ptr<Data> data, const Group& group,
                         const Publisher<Data>& publisher = Publisher<Data>())
    {
        publish_dynamic<Data, scheme>(std::shared_ptr<const Data>(data), group, publisher);
    }

    /// \brief Subscribe to a specific run-time defined group and data type (const reference variant). Where possible, prefer the static variant in StaticTransporterInterface::subscribe()
    ///
    /// \tparam Data data type to subscribe to.
    /// \tparam scheme Marshalling scheme id (typically MarshallingScheme::MarshallingSchemeEnum). Can usually be inferred from the Data type.
    /// \param f Callback function or lambda that is called upon receipt of the subscribed data
    /// \param group group to subscribe to (typically a DynamicGroup)
    /// \param subscriber Optional metadata that controls the subscription or sets callbacks to monitor the subscription result. Typically unnecessary for interprocess and inner layers.
    template <typename Data, int scheme = scheme<Data>()>
    void subscribe_dynamic(std::function<void(const Data&)> f, const Group& group,
                           const Subscriber<Data>& subscriber = Subscriber<Data>())
    {
        check_validity_runtime(group);
        static_cast<Derived*>(this)->template _subscribe<Data, scheme>(
            [=](std::shared_ptr<const Data> d) { f(*d); }, group, subscriber);
    }

    /// \brief Subscribe to a specific run-time defined group and data type (shared pointer variant). Where possible, prefer the static variant in StaticTransporterInterface::subscribe()
    ///
    /// \tparam Data data type to subscribe to.
    /// \tparam scheme Marshalling scheme id (typically MarshallingScheme::MarshallingSchemeEnum). Can usually be inferred from the Data type.
    /// \param f Callback function or lambda that is called upon receipt of the subscribed data
    /// \param group group to subscribe to (typically a DynamicGroup)
    /// \param subscriber Optional metadata that controls the subscription or sets callbacks to monitor the subscription result. Typically unnecessary for interprocess and inner layers.
    template <typename Data, int scheme = scheme<Data>()>
    void subscribe_dynamic(std::function<void(std::shared_ptr<const Data>)> f, const Group& group,
                           const Subscriber<Data>& subscriber = Subscriber<Data>())
    {
        check_validity_runtime(group);
        static_cast<Derived*>(this)->template _subscribe<Data, scheme>(f, group, subscriber);
    }

    /// \brief Unsubscribe to a specific run-time defined group and data type. Where possible, prefer the static variant in StaticTransporterInterface::unsubscribe()
    ///
    /// \tparam Data data type to unsubscribe from.
    /// \tparam scheme Marshalling scheme id (typically MarshallingScheme::MarshallingSchemeEnum). Can usually be inferred from the Data type.
    /// \param group group to unsubscribe from (typically a DynamicGroup)
    template <typename Data, int scheme = scheme<Data>()>
    void unsubscribe_dynamic(const Group& group)
    {
        check_validity_runtime(group);
        static_cast<Derived*>(this)->template _unsubscribe<Data, scheme>(group);
    }

    /// \brief Unsubscribe from all current subscriptions
    void unsubscribe_all() { static_cast<Derived*>(this)->_unsubscribe_all(); }

    /// \brief returns the marshalling scheme id for a given data type on this layer
    ///
    /// \tparam Data data type
    /// \return marshalling scheme id
    template <typename Data> static constexpr int scheme()
    {
        int scheme = goby::middleware::scheme<Data>();
        // if default returns DCCL, use PROTOBUF instead
        if (scheme == MarshallingScheme::DCCL)
            scheme = MarshallingScheme::PROTOBUF;
        return scheme;
    }

    /// \brief Check validity of the Group for interthread use (at compile time)
    ///
    /// This layer requires a valid string group
    template <const Group& group> void check_validity()
    {
        static_assert((group.c_str() != nullptr) && (group.c_str()[0] != '\0'),
                      "goby::middleware::Group must have non-zero length string to publish on the "
                      "InterModule layer");
    }

    /// \brief Check validity of the Group for interthread use (for DynamicGroup at run time)
    void check_validity_runtime(const Group& group)
    {
        if ((group.c_str() == nullptr) || (group.c_str()[0] == '\0'))
            throw(goby::Exception("Group must have a non-empty string for use on InterModule"));
    }

  protected:
    static constexpr Group to_portal_group_{"goby::middleware::intermodule::to_portal"};

    // add full_pid
    static const std::string from_portal_group_prefix_;

  private:
    friend PollerType;
    int _poll(std::unique_ptr<std::unique_lock<std::timed_mutex>>& lock)
    {
        return static_cast<Derived*>(this)->_poll(lock);
    }
};

template <typename Derived, typename InnerTransporter>
constexpr goby::middleware::Group
    InterModuleTransporterBase<Derived, InnerTransporter>::to_portal_group_;

template <typename Derived, typename InnerTransporter>
const std::string InterModuleTransporterBase<Derived, InnerTransporter>::from_portal_group_prefix_{
    "goby::middleware::intermodule::from_portal::"};

/// \brief Implements the forwarder concept for the interprocess layer
///
/// The forwarder is intended to be used by inner nodes within the layer that do not connect directly to other nodes on that layer. For example, the main thread might instantiate a portal and then spawn several threads that instantiate forwarders. These auxiliary threads can then communicate on the interprocess layer as if they had a direct connection to other interprocess nodes.
/// \tparam InnerTransporter The type of the inner transporter used to forward data to and from this node
template <typename InnerTransporter>
class InterModuleForwarder
    : public InterModuleTransporterBase<InterModuleForwarder<InnerTransporter>, InnerTransporter>
{
  public:
    using Base =
        InterModuleTransporterBase<InterModuleForwarder<InnerTransporter>, InnerTransporter>;

    /// \brief Construct a forwarder for the interprocess layer
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
