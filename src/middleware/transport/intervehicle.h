// Copyright 2016-2024:
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

#ifndef GOBY_MIDDLEWARE_TRANSPORT_INTERVEHICLE_H
#define GOBY_MIDDLEWARE_TRANSPORT_INTERVEHICLE_H

#include <atomic>
#include <functional>
#include <sys/types.h>
#include <thread>
#include <unistd.h>

#include <google/protobuf/io/zero_copy_stream_impl.h>

#include "goby/middleware/marshalling/dccl.h"

#include "goby/middleware/protobuf/intervehicle.pb.h"
#include "goby/middleware/transport/interthread.h" // used for InterVehiclePortal implementation
#include "goby/middleware/transport/intervehicle/driver_thread.h"
#include "goby/middleware/transport/intervehicle/groups.h"
#include "goby/middleware/transport/serialization_handlers.h"

namespace goby
{
namespace middleware
{
class InvalidSubscription : public Exception
{
  public:
    InvalidSubscription(const std::string& e) : Exception(e) {}
};

class InvalidPublication : public Exception
{
  public:
    InvalidPublication(const std::string& e) : Exception(e) {}
};

class InvalidUnsubscription : public Exception
{
  public:
    InvalidUnsubscription(const std::string& e) : Exception(e) {}
};

/// \brief Base class for implementing transporters (both portal and forwarder) for the intervehicle layer
///
/// \tparam Derived derived class (curiously recurring template pattern)
/// \tparam InnerTransporter inner layer transporter type
template <typename Derived, typename InnerTransporter>
class InterVehicleTransporterBase
    : public StaticTransporterInterface<InterVehicleTransporterBase<Derived, InnerTransporter>,
                                        InnerTransporter>,
      public Poller<InterVehicleTransporterBase<Derived, InnerTransporter>>
{
    using InterfaceType =
        StaticTransporterInterface<InterVehicleTransporterBase<Derived, InnerTransporter>,
                                   InnerTransporter>;

    using PollerType = Poller<InterVehicleTransporterBase<Derived, InnerTransporter>>;

  public:
    enum class SubscriptionAction
    {
        SUBSCRIBE,
        UNSUBSCRIBE
    };

    InterVehicleTransporterBase(InnerTransporter& inner)
        : InterfaceType(inner), PollerType(&this->inner())
    {
        // handle request from Portal to omit or include metadata on future publications for a given data type
        this->inner()
            .template subscribe<intervehicle::groups::metadata_request,
                                protobuf::SerializerMetadataRequest>(
                [this](const protobuf::SerializerMetadataRequest& request)
                {
                    glog.is_debug3() && glog << "Received DCCL metadata request: "
                                             << request.ShortDebugString() << std::endl;

                    switch (request.request())
                    {
                        case protobuf::SerializerMetadataRequest::METADATA_INCLUDE:
                            omit_publish_metadata_.erase(request.key().type());
                            break;
                        case protobuf::SerializerMetadataRequest::METADATA_EXCLUDE:
                            omit_publish_metadata_.insert(request.key().type());
                            break;
                    }
                });
    }
    InterVehicleTransporterBase() : PollerType(&this->inner()) {}

    virtual ~InterVehicleTransporterBase() = default;

    /// \brief returns the marshalling scheme id for a given data type on this layer. Only MarshallingScheme::DCCL is currently supported
    template <typename Data> static constexpr int scheme()
    {
        static_assert(goby::middleware::scheme<typename detail::primitive_type<Data>::type>() ==
                          MarshallingScheme::DCCL,
                      "Can only use DCCL messages with InterVehicleTransporters");
        return MarshallingScheme::DCCL;
    }

    /// \brief Check validity of the Group for interthread use (at compile time)
    ///
    /// The layer requires a valid numeric group
    template <const Group& group> void check_validity()
    {
        static_assert(group.numeric() != Group::invalid_numeric_group,
                      "goby::middleware::Group must have non-zero numeric "
                      "value to publish on the InterVehicle layer");
    }

    /// \brief Publish a message using a run-time defined DynamicGroup (const reference variant). Where possible, prefer the static variant in StaticTransporterInterface::publish()
    ///
    /// \tparam Data data type to publish. Can usually be inferred from the \c data parameter.
    /// \tparam scheme Marshalling scheme id (typically MarshallingScheme::MarshallingSchemeEnum). Can usually be inferred from the Data type.
    /// \param data Message to publish
    /// \param group group to publish this message to (typically a DynamicGroup). If a Publisher is provided, the group will be set in the data using Publisher::set_group()
    /// \param publisher Optional metadata that controls the publication or sets callbacks to monitor the result.
    template <typename Data, int scheme = goby::middleware::scheme<Data>()>
    void publish_dynamic(const Data& data, const Group& group = Group(),
                         const Publisher<Data>& publisher = Publisher<Data>())
    {
        static_assert(scheme == MarshallingScheme::DCCL,
                      "Can only use DCCL messages with InterVehicleTransporters");

        Data data_with_group = data;
        publisher.set_group(data_with_group, group);

        static_cast<Derived*>(this)->template _publish<Data>(data_with_group, group, publisher);
        // publish to interprocess as both DCCL and Protobuf
        this->inner().template publish_dynamic<Data, MarshallingScheme::DCCL>(data_with_group,
                                                                              group, publisher);
        this->inner().template publish_dynamic<Data, MarshallingScheme::PROTOBUF>(data_with_group,
                                                                                  group, publisher);
    }

    /// \brief Publish a message using a run-time defined DynamicGroup (shared pointer to const data variant). Where possible, prefer the static variant in StaticTransporterInterface::publish()
    ///
    /// \tparam Data data type to publish. Can usually be inferred from the \c data parameter.
    /// \tparam scheme Marshalling scheme id (typically MarshallingScheme::MarshallingSchemeEnum). Can usually be inferred from the Data type.
    /// \param data Message to publish
    /// \param group group to publish this message to (typically a DynamicGroup). If a Publisher is provided, the group will be set in the data using Publisher::set_group()
    /// \param publisher Optional metadata that controls the publication or sets callbacks to monitor the result.
    template <typename Data, int scheme = goby::middleware::scheme<Data>()>
    void publish_dynamic(std::shared_ptr<const Data> data, const Group& group = Group(),
                         const Publisher<Data>& publisher = Publisher<Data>())
    {
        static_assert(scheme == MarshallingScheme::DCCL,
                      "Can only use DCCL messages with InterVehicleTransporters");
        if (data)
        {
            // copy this way as it allows us to copy Data == google::protobuf::Message abstract base class
            std::shared_ptr<Data> data_with_group(data->New());
            data_with_group->CopyFrom(*data);

            publisher.set_group(*data_with_group, group);

            static_cast<Derived*>(this)->template _publish<Data>(*data_with_group, group,
                                                                 publisher);

            // publish to interprocess as both DCCL and Protobuf
            this->inner().template publish_dynamic<Data, MarshallingScheme::DCCL>(data_with_group,
                                                                                  group, publisher);
            this->inner().template publish_dynamic<Data, MarshallingScheme::PROTOBUF>(
                data_with_group, group, publisher);
        }
    }

    /// \brief Publish a message using a run-time defined DynamicGroup (shared pointer to mutable data variant). Where possible, prefer the static variant in StaticTransporterInterface::publish()
    ///
    /// \tparam Data data type to publish. Can usually be inferred from the \c data parameter.
    /// \tparam scheme Marshalling scheme id (typically MarshallingScheme::MarshallingSchemeEnum). Can usually be inferred from the Data type.
    /// \param data Message to publish
    /// \param group group to publish this message to (typically a DynamicGroup). If a Publisher is provided, the group will be set in the data using Publisher::set_group()
    /// \param publisher Optional metadata that controls the publication or sets callbacks to monitor the result.
    template <typename Data, int scheme = goby::middleware::scheme<Data>()>
    void publish_dynamic(std::shared_ptr<Data> data, const Group& group = Group(),
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
    template <typename Data, int scheme = goby::middleware::scheme<Data>()>
    void subscribe_dynamic(std::function<void(const Data&)> f, const Group& group = Group(),
                           const Subscriber<Data>& subscriber = Subscriber<Data>())
    {
        static_assert(scheme == MarshallingScheme::DCCL,
                      "Can only use DCCL messages with InterVehicleTransporters");
        auto pointer_ref_lambda = [=](std::shared_ptr<const Data> d) { f(*d); };
        static_cast<Derived*>(this)->template _subscribe<Data>(
            pointer_ref_lambda, group, subscriber, SubscriptionAction::SUBSCRIBE);
    }

    /// \brief Subscribe to a specific run-time defined group and data type (shared pointer variant). Where possible, prefer the static variant in StaticTransporterInterface::subscribe()
    ///
    /// \tparam Data data type to subscribe to.
    /// \tparam scheme Marshalling scheme id (typically MarshallingScheme::MarshallingSchemeEnum). Can usually be inferred from the Data type.
    /// \param f Callback function or lambda that is called upon receipt of the subscribed data
    /// \param group group to subscribe to (typically a DynamicGroup)
    /// \param subscriber Optional metadata that controls the subscription or sets callbacks to monitor the subscription result. Typically unnecessary for interprocess and inner layers.
    template <typename Data, int scheme = goby::middleware::scheme<Data>()>
    void subscribe_dynamic(std::function<void(std::shared_ptr<const Data>)> f,
                           const Group& group = Group(),
                           const Subscriber<Data>& subscriber = Subscriber<Data>())
    {
        static_assert(scheme == MarshallingScheme::DCCL,
                      "Can only use DCCL messages with InterVehicleTransporters");
        static_cast<Derived*>(this)->template _subscribe<Data>(f, group, subscriber,
                                                               SubscriptionAction::SUBSCRIBE);
    }

    /// \brief Unsubscribe from a specific run-time defined group and data type. Where possible, prefer the static variant in StaticTransporterInterface::unsubscribe()
    ///
    /// \tparam Data data type to unsubscribe from.
    /// \tparam scheme Marshalling scheme id (typically MarshallingScheme::MarshallingSchemeEnum). Can usually be inferred from the Data type.
    /// \param group group to subscribe to (typically a DynamicGroup)
    /// \param subscriber Optional metadata that controls the subscription or sets callbacks to monitor the subscription result. Typically unnecessary for interprocess and inner layers.
    template <typename Data, int scheme = goby::middleware::scheme<Data>()>
    void unsubscribe_dynamic(const Group& group = Group(),
                             const Subscriber<Data>& subscriber = Subscriber<Data>())
    {
        static_assert(scheme == MarshallingScheme::DCCL,
                      "Can only use DCCL messages with InterVehicleTransporters");
        static_cast<Derived*>(this)->template _subscribe<Data>(
            std::function<void(std::shared_ptr<const Data>)>(), group, subscriber,
            SubscriptionAction::UNSUBSCRIBE);
    }

  protected:
    template <typename Data>
    std::shared_ptr<goby::middleware::protobuf::SerializerTransporterMessage>
    _set_up_publish(const Data& d, const Group& group, const Publisher<Data>& publisher)
    {
        if (group.numeric() != Group::broadcast_group && !publisher.has_set_group_func())
        {
            std::stringstream ss;
            ss << "Error: Publisher must have set_group_func in order to publish to a "
                  "non-broadcast Group ("
               << group
               << "). The set_group_func modifies the contents of the outgoing message to store "
                  "the group information.";
            throw(InvalidPublication(ss.str()));
        }

        auto data = intervehicle::serialize_publication(d, group, publisher);

        if (publisher.cfg().intervehicle().buffer().ack_required())
        {
            auto ack_handler = std::make_shared<
                PublisherCallback<Data, MarshallingScheme::DCCL, intervehicle::protobuf::AckData>>(
                publisher.acked_func(), d);

            auto expire_handler =
                std::make_shared<PublisherCallback<Data, MarshallingScheme::DCCL,
                                                   intervehicle::protobuf::ExpireData>>(
                    publisher.expired_func(), d);

            this->_insert_pending_ack(SerializerParserHelper<Data, MarshallingScheme::DCCL>::id(d),
                                      data, ack_handler, expire_handler);
        }

        if (!omit_publish_metadata_.count(data->key().type()))
            _set_protobuf_metadata<Data>(data->mutable_key()->mutable_metadata(), d);

        goby::glog.is_debug3() &&
            goby::glog << "Set up publishing for: " << data->ShortDebugString() << std::endl;

        return data;
    }

    template <typename Data>
    std::shared_ptr<intervehicle::protobuf::Subscription>
    _set_up_subscribe(std::function<void(std::shared_ptr<const Data> d)> func, const Group& group,
                      const Subscriber<Data>& subscriber, SubscriptionAction action)
    {
        auto dccl_id = SerializerParserHelper<Data, MarshallingScheme::DCCL>::id();

        switch (action)
        {
            case SubscriptionAction::SUBSCRIBE:
            {
                if (group.numeric() != Group::broadcast_group && !subscriber.has_group_func())
                {
                    std::stringstream ss;
                    ss << "Error: Subscriber must have group_func in order to subscribe to "
                          "non-broadcast Group ("
                       << group
                       << "). The group_func returns the appropriate Group based on the contents "
                          "of the incoming message.";
                    throw(InvalidSubscription(ss.str()));
                }

                if (subscriber.cfg().intervehicle().broadcast() &&
                    subscriber.cfg().intervehicle().buffer().ack_required())
                {
                    std::stringstream ss;
                    ss << "Error: Broadcast subscriptions cannot have ack_required: true";
                    throw(InvalidSubscription(ss.str()));
                }

                auto subscription = std::make_shared<
                    IntervehicleSerializationSubscription<Data, MarshallingScheme::DCCL>>(
                    func, group, subscriber);

                this->subscriptions_[dccl_id][group] = subscription;
            }
            break;
            case SubscriptionAction::UNSUBSCRIBE:
            {
                auto sub_it = this->subscriptions_[dccl_id].find(group);
                if (sub_it != this->subscriptions_[dccl_id].end())
                {
                    this->subscriptions_[dccl_id].erase(sub_it);
                }
                else
                {
                    std::stringstream ss;
                    ss << "Cannot unsubscribe to DCCL id: " << dccl_id
                       << " and group: " << std::string(group) << " as no subscription was found.";
                    throw(InvalidUnsubscription(ss.str()));
                }
            }
            break;
        }

        auto dccl_subscription =
            this->template _serialize_subscription<Data>(group, subscriber, action);
        using intervehicle::protobuf::Subscription;
        // insert pending subscription
        auto subscription_publication = intervehicle::serialize_publication(
            *dccl_subscription, intervehicle::groups::subscription_forward,
            Publisher<Subscription>());

        // overwrite timestamps to ensure mapping with driver threads
        auto subscribe_time = dccl_subscription->time_with_units();
        subscription_publication->mutable_key()->set_serialize_time_with_units(subscribe_time);

        auto ack_handler = std::make_shared<PublisherCallback<Subscription, MarshallingScheme::DCCL,
                                                              intervehicle::protobuf::AckData>>(
            subscriber.subscribed_func());

        auto expire_handler =
            std::make_shared<PublisherCallback<Subscription, MarshallingScheme::DCCL,
                                               intervehicle::protobuf::ExpireData>>(
                subscriber.subscribe_expired_func());

        goby::glog.is_debug1() && goby::glog << "Inserting subscription ack handler for "
                                             << subscription_publication->ShortDebugString()
                                             << std::endl;

        this->pending_ack_.insert(std::make_pair(*subscription_publication,
                                                 std::make_tuple(ack_handler, expire_handler)));

        return dccl_subscription;
    }

    template <int tuple_index, typename AckorExpirePair>
    void _handle_ack_or_expire(const AckorExpirePair& ack_or_expire_pair)
    {
        auto original = ack_or_expire_pair.serializer();
        const auto& ack_or_expire_msg = ack_or_expire_pair.data();
        bool is_subscription = original.key().marshalling_scheme() == MarshallingScheme::DCCL &&
                               original.key().type() ==
                                   intervehicle::protobuf::Subscription::descriptor()->full_name();

        if (is_subscription)
        {
            // rewrite data to remove src()
            auto bytes_begin = original.data().begin(), bytes_end = original.data().end();
            decltype(bytes_begin) actual_end;

            using Helper = SerializerParserHelper<intervehicle::protobuf::Subscription,
                                                  MarshallingScheme::DCCL>;
            auto subscription = Helper::parse(bytes_begin, bytes_end, actual_end);
            subscription->mutable_header()->set_src(0);

            std::vector<char> bytes(Helper::serialize(*subscription));
            std::string* sbytes = new std::string(bytes.begin(), bytes.end());
            original.set_allocated_data(sbytes);
        }

        auto it = pending_ack_.find(original);
        if (it != pending_ack_.end())
        {
            goby::glog.is_debug3() && goby::glog << ack_or_expire_msg.GetDescriptor()->name()
                                                 << " for: " << original.ShortDebugString() << ", "
                                                 << ack_or_expire_msg.ShortDebugString()
                                                 << std::endl;

            std::get<tuple_index>(it->second)
                ->post(original.data().begin(), original.data().end(), ack_or_expire_msg);
        }
        else
        {
            goby::glog.is_debug3() && goby::glog << "No pending Ack/Expire for "
                                                 << (is_subscription ? "subscription: " : "data: ")
                                                 << original.ShortDebugString() << std::endl;
        }
    }

    void _receive(const intervehicle::protobuf::DCCLForwardedData& packets)
    {
        goby::glog.is_debug3() && goby::glog << "Received DCCLForwarded data: "
                                             << packets.ShortDebugString() << std::endl;

        for (const auto& packet : packets.frame())
        {
            for (auto p : this->subscriptions_[packet.dccl_id()])
                p.second->post(packet.data().begin(), packet.data().end(), packets.header());
        }
    }

    template <typename Data>
    std::shared_ptr<intervehicle::protobuf::Subscription>
    _serialize_subscription(const Group& group, const Subscriber<Data>& subscriber,
                            SubscriptionAction action)
    {
        auto dccl_id = SerializerParserHelper<Data, MarshallingScheme::DCCL>::id();
        auto dccl_subscription = std::make_shared<intervehicle::protobuf::Subscription>();
        dccl_subscription->mutable_header()->set_src(0);

        for (auto id : subscriber.cfg().intervehicle().publisher_id())
            dccl_subscription->mutable_header()->add_dest(id);

        dccl_subscription->set_api_version(GOBY_INTERVEHICLE_API_VERSION);
        dccl_subscription->set_dccl_id(dccl_id);
        dccl_subscription->set_group(group.numeric());
        dccl_subscription->set_time_with_units(
            goby::time::SystemClock::now<goby::time::MicroTime>());
        dccl_subscription->set_action((action == SubscriptionAction::SUBSCRIBE)
                                          ? intervehicle::protobuf::Subscription::SUBSCRIBE
                                          : intervehicle::protobuf::Subscription::UNSUBSCRIBE);

        _set_protobuf_metadata<Data>(dccl_subscription->mutable_metadata());
        *dccl_subscription->mutable_intervehicle() = subscriber.cfg().intervehicle();
        return dccl_subscription;
    }

    void _insert_pending_ack(
        int dccl_id, std::shared_ptr<goby::middleware::protobuf::SerializerTransporterMessage> data,
        std::shared_ptr<SerializationHandlerBase<intervehicle::protobuf::AckData>> ack_handler,
        std::shared_ptr<SerializationHandlerBase<intervehicle::protobuf::ExpireData>>
            expire_handler)
    {
        goby::glog.is_debug3() && goby::glog << "Inserting ack handler for "
                                             << data->ShortDebugString() << std::endl;

        this->pending_ack_.insert(
            std::make_pair(*data, std::make_tuple(ack_handler, expire_handler)));
    }

  protected:
    // maps DCCL ID to map of Group->subscription
    // only one subscription allowed per IntervehicleForwarder/Portal (new subscription overwrites old one)
    std::unordered_map<
        int, std::unordered_map<std::string, std::shared_ptr<const SerializationHandlerBase<
                                                 intervehicle::protobuf::Header>>>>
        subscriptions_;

  private:
    friend PollerType;
    int _poll(std::unique_ptr<std::unique_lock<std::timed_mutex>>& lock)
    {
        _expire_pending_ack();

        return static_cast<Derived*>(this)->_poll(lock);
    }

    template <typename Data> void _set_protobuf_metadata(protobuf::SerializerProtobufMetadata* meta)
    {
        meta->set_protobuf_name(SerializerParserHelper<Data, MarshallingScheme::DCCL>::type_name());
        _insert_file_desc_with_dependencies(Data::descriptor()->file(), meta);
    }

    template <typename Data>
    void _set_protobuf_metadata(protobuf::SerializerProtobufMetadata* meta, const Data& d)
    {
        meta->set_protobuf_name(
            SerializerParserHelper<Data, MarshallingScheme::DCCL>::type_name(d));
        _insert_file_desc_with_dependencies(d.GetDescriptor()->file(), meta);
    }

    // used to populated InterVehicleSubscription file_descriptor fields
    void _insert_file_desc_with_dependencies(const google::protobuf::FileDescriptor* file_desc,
                                             protobuf::SerializerProtobufMetadata* meta)
    {
        for (int i = 0, n = file_desc->dependency_count(); i < n; ++i)
            _insert_file_desc_with_dependencies(file_desc->dependency(i), meta);

        google::protobuf::FileDescriptorProto* file_desc_proto = meta->add_file_descriptor();
        file_desc->CopyTo(file_desc_proto);
    }

    // expire any pending_ack entries that are no longer relevant
    void _expire_pending_ack()
    {
        auto now = goby::time::SystemClock::now<goby::time::MicroTime>();
        for (auto it = pending_ack_.begin(), end = pending_ack_.end(); it != end;)
        {
            decltype(now) max_ttl(goby::acomms::protobuf::DynamicBufferConfig::descriptor()
                                      ->FindFieldByName("ttl")
                                      ->options()
                                      .GetExtension(dccl::field)
                                      .max() *
                                  goby::acomms::protobuf::DynamicBufferConfig::ttl_unit());

            decltype(now) serialize_time(it->first.key().serialize_time_with_units());
            decltype(now) expire_time(serialize_time + max_ttl);

            // time to let any expire messages from the drivers propagate through the interprocess layer before we remove this
            const decltype(now) interprocess_wait(1.0 * boost::units::si::seconds);

            // loop through pending ack, and clear any at the front that can be removed

            if (now > expire_time + interprocess_wait)
            {
                goby::glog.is_debug3() && goby::glog << "Erasing pending ack for "
                                                     << it->first.ShortDebugString() << std::endl;
                it = pending_ack_.erase(it);
            }
            else
            {
                // pending_ack_ is ordered by serialize time, so we can bail now
                break;
            }
        }
    }

  private:
    // maps data with ack_requested onto callbacks for when the data are acknowledged or expire
    // ordered by serialize time
    std::map<
        protobuf::SerializerTransporterMessage,
        std::tuple<std::shared_ptr<SerializationHandlerBase<intervehicle::protobuf::AckData>>,
                   std::shared_ptr<SerializationHandlerBase<intervehicle::protobuf::ExpireData>>>>
        pending_ack_;

    // map of Protobuf names where we can omit metadata on publication
    std::set<std::string> omit_publish_metadata_;
};

/// \brief Implements the forwarder concept for the intervehicle layer
///
/// This forwarder is used by applications that do not directly communicate with other vehicles, but are connected on the interprocess layer. For example, \c gobyd instantiates a portal and other processes running on the vehicle can transmit and receive data through that portal via the use of this forwarder.
/// \tparam InnerTransporter The type of the inner transporter used to forward data to and from this node
template <typename InnerTransporter>
class InterVehicleForwarder
    : public InterVehicleTransporterBase<InterVehicleForwarder<InnerTransporter>, InnerTransporter>
{
  public:
    using Base =
        InterVehicleTransporterBase<InterVehicleForwarder<InnerTransporter>, InnerTransporter>;

    /// \brief Construct a forwarder for the intervehicle layer
    ///
    /// \param inner A reference to the inner transporter used to forward messages to and from the portal
    InterVehicleForwarder(InnerTransporter& inner) : Base(inner)
    {
        this->inner()
            .template subscribe<intervehicle::groups::modem_data_in,
                                intervehicle::protobuf::DCCLForwardedData>(
                [this](const intervehicle::protobuf::DCCLForwardedData& msg)
                { this->_receive(msg); });

        using ack_pair_type = intervehicle::protobuf::AckMessagePair;
        this->inner().template subscribe<intervehicle::groups::modem_ack_in, ack_pair_type>(
            [this](const ack_pair_type& ack_pair)
            { this->template _handle_ack_or_expire<0>(ack_pair); });

        using expire_pair_type = intervehicle::protobuf::ExpireMessagePair;
        this->inner().template subscribe<intervehicle::groups::modem_expire_in, expire_pair_type>(
            [this](const expire_pair_type& expire_pair)
            { this->template _handle_ack_or_expire<1>(expire_pair); });
    }

    virtual ~InterVehicleForwarder() = default;

    friend Base;

  private:
    template <typename Data>
    void _publish(const Data& d, const Group& group, const Publisher<Data>& publisher)
    {
        this->inner().template publish<intervehicle::groups::modem_data_out>(
            this->_set_up_publish(d, group, publisher));
    }

    template <typename Data>
    void _subscribe(std::function<void(std::shared_ptr<const Data> d)> func, const Group& group,
                    const Subscriber<Data>& subscriber, typename Base::SubscriptionAction action)
    {
        try
        {
            this->inner()
                .template publish<intervehicle::groups::modem_subscription_forward_tx,
                                  intervehicle::protobuf::Subscription,
                                  MarshallingScheme::PROTOBUF>(
                    this->_set_up_subscribe(func, group, subscriber, action));
        }
        catch (const InvalidUnsubscription& e)
        {
            goby::glog.is_warn() && goby::glog << e.what() << std::endl;
        }
    }

    int _poll(std::unique_ptr<std::unique_lock<std::timed_mutex>>& lock) { return 0; }
};

/// \brief Implements a portal for the intervehicle layer based on Goby Acomms.
///
/// \tparam InnerTransporter The type of the inner transporter used to forward data to and from this node. This portal uses goby::middleware::InterThreadTransport internally, so the innermost transporter must be goby::middleware::InterThreadTransport. This allows for use of any InterProcessPortal, as long as that InterProcessPortal has an inner transporter of goby::middleware::InterThreadTransport.
template <typename InnerTransporter>
class InterVehiclePortal
    : public InterVehicleTransporterBase<InterVehiclePortal<InnerTransporter>, InnerTransporter>
{
    using modem_id_type = goby::middleware::intervehicle::ModemDriverThread::modem_id_type;

  public:
    using Base =
        InterVehicleTransporterBase<InterVehiclePortal<InnerTransporter>, InnerTransporter>;

    /// \brief Instantiate a portal with the given configuration (with the portal owning the inner transporter)
    ///
    /// \param cfg Configuration of physical modem links to use, etc.
    InterVehiclePortal(const intervehicle::protobuf::PortalConfig& cfg) : cfg_(cfg) { _init(); }

    /// \brief Instantiate a portal with the given configuration and a reference to an external inner transporter
    ///
    /// \param inner Reference to the inner transporter to use
    /// \param cfg Configuration of physical modem links to use, etc.
    InterVehiclePortal(InnerTransporter& inner, const intervehicle::protobuf::PortalConfig& cfg)
        : Base(inner), cfg_(cfg)
    {
        _init();
    }

    ~InterVehiclePortal()
    {
        for (auto& modem_driver_data : modem_drivers_)
        {
            modem_driver_data->driver_thread_alive = false;
            if (modem_driver_data->underlying_thread)
                modem_driver_data->underlying_thread->join();
        }
    }

    friend Base;

  private:
    template <typename Data>
    void _publish(const Data& d, const Group& group, const Publisher<Data>& publisher)
    {
        this->innermost().template publish<intervehicle::groups::modem_data_out>(
            this->_set_up_publish(d, group, publisher));
    }

    template <typename Data>
    void _subscribe(std::function<void(std::shared_ptr<const Data> d)> func, const Group& group,
                    const Subscriber<Data>& subscriber, typename Base::SubscriptionAction action)
    {
        try
        {
            auto dccl_subscription = this->_set_up_subscribe(func, group, subscriber, action);

            this->innermost().template publish<intervehicle::groups::modem_subscription_forward_tx>(
                dccl_subscription);
        }
        catch (const InvalidUnsubscription& e)
        {
            goby::glog.is_warn() && goby::glog << e.what() << std::endl;
        }
    }

    int _poll(std::unique_ptr<std::unique_lock<std::timed_mutex>>& lock)
    {
        int items = 0;
        goby::acomms::protobuf::ModemTransmission msg;
        while (!received_.empty())
        {
            this->_receive(received_.front());
            received_.pop_front();
            ++items;
            if (lock)
                lock.reset();
        }
        return items;
    }

    void _init()
    {
        // set up reception of forwarded (via acoustic) subscriptions,
        // then re-publish to driver threads
        {
            using intervehicle::protobuf::Subscription;
            auto subscribe_lambda = [=](std::shared_ptr<const Subscription> d)
            {
                this->innermost()
                    .template publish<intervehicle::groups::modem_subscription_forward_rx,
                                      intervehicle::protobuf::Subscription,
                                      MarshallingScheme::PROTOBUF>(d);
            };
            auto subscription = std::make_shared<
                IntervehicleSerializationSubscription<Subscription, MarshallingScheme::DCCL>>(
                subscribe_lambda);

            auto dccl_id = SerializerParserHelper<Subscription, MarshallingScheme::DCCL>::id();
            this->subscriptions_[dccl_id].insert(
                std::make_pair(subscription->subscribed_group(), subscription));
        }

        this->innermost().template subscribe<intervehicle::groups::modem_data_in>(
            [this](const intervehicle::protobuf::DCCLForwardedData& msg)
            { received_.push_back(msg); });

        // a message requiring ack can be disposed by either [1] ack, [2] expire (TTL exceeded), [3] having no subscribers, [4] queue size exceeded.
        // post the correct callback (ack for [1] and expire for [2-4])
        // and remove the pending ack message
        using ack_pair_type = intervehicle::protobuf::AckMessagePair;
        this->innermost().template subscribe<intervehicle::groups::modem_ack_in, ack_pair_type>(
            [this](const ack_pair_type& ack_pair)
            { this->template _handle_ack_or_expire<0>(ack_pair); });

        using expire_pair_type = intervehicle::protobuf::ExpireMessagePair;
        this->innermost()
            .template subscribe<intervehicle::groups::modem_expire_in, expire_pair_type>(
                [this](const expire_pair_type& expire_pair)
                { this->template _handle_ack_or_expire<1>(expire_pair); });

        this->innermost().template subscribe<intervehicle::groups::modem_driver_ready, bool>(
            [this](const bool& ready)
            {
                goby::glog.is_debug1() && goby::glog << "Received driver ready" << std::endl;
                ++drivers_ready_;
            });

        // set up before drivers ready to ensure we don't miss subscriptions
        if (cfg_.has_persist_subscriptions())
            _set_up_persistent_subscriptions();

        for (int i = 0, n = cfg_.link_size(); i < n; ++i)
        {
            auto* link = cfg_.mutable_link(i);

            link->mutable_driver()->set_modem_id(link->modem_id());
            link->mutable_mac()->set_modem_id(link->modem_id());

            modem_drivers_.emplace_back(new ModemDriverData);
            ModemDriverData& data = *modem_drivers_.back();

            data.underlying_thread.reset(new std::thread(
                [&data, link]()
                {
                    try
                    {
                        data.modem_driver_thread.reset(new intervehicle::ModemDriverThread(*link));
                        data.modem_driver_thread->run(data.driver_thread_alive);
                    }
                    catch (std::exception& e)
                    {
                        goby::glog.is_warn() &&
                            goby::glog << "Modem driver thread had uncaught exception: " << e.what()
                                       << std::endl;
                        throw;
                    }
                }));

            if (goby::glog.buf().is_gui())
                // allows for visual grouping of each link in the NCurses gui
                std::this_thread::sleep_for(std::chrono::milliseconds(250));
        }

        while (drivers_ready_ < modem_drivers_.size())
        {
            goby::glog.is_debug1() && goby::glog << "Waiting for drivers to be ready." << std::endl;
            this->poll();
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }

        // write subscriptions after drivers ready to ensure they aren't missed
        if (former_sub_collection_.subscription_size() > 0)
        {
            goby::glog.is_debug1() &&
                goby::glog << "Begin loading subscriptions from persistent storage..." << std::endl;
            for (const auto& sub : former_sub_collection_.subscription())
                this->innermost()
                    .template publish<intervehicle::groups::modem_subscription_forward_rx,
                                      intervehicle::protobuf::Subscription,
                                      MarshallingScheme::PROTOBUF>(sub);
        }
    }

    void _set_up_persistent_subscriptions()
    {
        const auto& dir = cfg_.persist_subscriptions().dir();
        if (dir.empty())
            goby::glog.is_die() && goby::glog << "persist_subscriptions.dir cannot be empty"
                                              << std::endl;

        std::stringstream file_name;
        file_name << dir;
        if (dir.back() != '/')
            file_name << "/";
        file_name << "goby_intervehicle_subscriptions_" << cfg_.persist_subscriptions().name()
                  << ".pb.txt";
        persist_sub_file_name_ = file_name.str();
        {
            std::ifstream persist_sub_ifs(persist_sub_file_name_.c_str());
            try
            {
                if (persist_sub_ifs.is_open())
                {
                    google::protobuf::TextFormat::Parser parser;
                    google::protobuf::io::IstreamInputStream iis(&persist_sub_ifs);
                    parser.Parse(&iis, &former_sub_collection_);
                }
                else
                {
                    goby::glog.is_debug1() &&
                        goby::glog << "Could not open persistent subscriptions file: "
                                   << persist_sub_file_name_
                                   << ". Assuming no persistent subscriptions exist" << std::endl;
                }
            }
            catch (const std::exception& e)
            {
                goby::glog.is_warn() &&
                    goby::glog << "Error reading persistent subscriptions file: " << e.what()
                               << std::endl;
            }
        }

        std::ofstream persist_sub_ofs(persist_sub_file_name_.c_str());
        if (!persist_sub_ofs.is_open())
        {
            goby::glog.is_die() &&
                goby::glog << "Could not open persistent subscriptions file for writing: "
                           << persist_sub_file_name_ << std::endl;
        }
        remove(persist_sub_file_name_.c_str());

        this->innermost().template subscribe<intervehicle::groups::subscription_report>(
            [this](const intervehicle::protobuf::SubscriptionReport& report)
            {
                goby::glog.is_debug1() && goby::glog << "Received subscription report: "
                                                     << report.ShortDebugString() << std::endl;
                sub_reports_[report.link_modem_id()] = report;
                std::ofstream persist_sub_ofs(persist_sub_file_name_.c_str());
                intervehicle::protobuf::SubscriptionPersistCollection collection;
                collection.set_time_with_units(
                    goby::time::SystemClock::now<goby::time::MicroTime>());
                for (auto report_p : sub_reports_)
                {
                    for (const auto& sub : report_p.second.subscription())
                        *collection.add_subscription() = sub;
                }
                google::protobuf::TextFormat::Printer printer;
                google::protobuf::io::OstreamOutputStream oos(&persist_sub_ofs);
                goby::glog.is_debug1() &&
                    goby::glog << "Collection: " << collection.ShortDebugString() << std::endl;
                printer.Print(collection, &oos);
            });
    }

  private:
    intervehicle::protobuf::PortalConfig cfg_;

    struct ModemDriverData
    {
        std::unique_ptr<std::thread> underlying_thread;
        std::unique_ptr<intervehicle::ModemDriverThread> modem_driver_thread;
        std::atomic<bool> driver_thread_alive{true};
    };
    std::vector<std::unique_ptr<ModemDriverData>> modem_drivers_;
    unsigned drivers_ready_{0};

    std::deque<intervehicle::protobuf::DCCLForwardedData> received_;

    intervehicle::protobuf::SubscriptionPersistCollection former_sub_collection_;
    std::string persist_sub_file_name_;
    std::map<modem_id_type, intervehicle::protobuf::SubscriptionReport> sub_reports_;
};
} // namespace middleware
} // namespace goby

#endif
