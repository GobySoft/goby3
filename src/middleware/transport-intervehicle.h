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

#ifndef TransportInterVehicle20160810H
#define TransportInterVehicle20160810H

#include <atomic>
#include <functional>
#include <sys/types.h>
#include <thread>
#include <unistd.h>

#include "goby/middleware/protobuf/intervehicle.pb.h"
#include "goby/middleware/transport-common.h"
#include "goby/middleware/transport-interthread.h" // used for InterVehiclePortal implementation

#include "goby/middleware/intervehicle/driver-thread.h"

namespace std
{
template <> struct hash<goby::middleware::protobuf::SerializerTransporterMessage>
{
    size_t
    operator()(const goby::middleware::protobuf::SerializerTransporterMessage& msg) const noexcept
    {
        return std::hash<std::string>{}(msg.SerializeAsString());
    }
};
} // namespace std

namespace goby
{
namespace middleware
{
template <typename Derived, typename InnerTransporter>
class InterVehicleTransporterBase
    : public StaticTransporterInterface<InterVehicleTransporterBase<Derived, InnerTransporter>,
                                        InnerTransporter>,
      public Poller<InterVehicleTransporterBase<Derived, InnerTransporter> >
{
    using PollerType = Poller<InterVehicleTransporterBase<Derived, InnerTransporter> >;

  public:
    InterVehicleTransporterBase(InnerTransporter& inner) : PollerType(&inner), inner_(inner) {}
    InterVehicleTransporterBase(InnerTransporter* inner_ptr = new InnerTransporter,
                                bool base_owns_inner = true)
        : PollerType(inner_ptr),
          own_inner_(base_owns_inner ? inner_ptr : nullptr),
          inner_(*inner_ptr)
    {
    }

    virtual ~InterVehicleTransporterBase() = default;

    template <typename Data> static constexpr int scheme()
    {
        static_assert(goby::middleware::scheme<typename detail::primitive_type<Data>::type>() ==
                          MarshallingScheme::DCCL,
                      "Can only use DCCL messages with InterVehicleTransporters");
        return MarshallingScheme::DCCL;
    }

    template <const Group& group> void check_validity()
    {
        static_assert(group.numeric() != Group::invalid_numeric_group,
                      "goby::middleware::Group must have non-zero numeric "
                      "value to publish on the InterVehicle layer");
    }

    // implements StaticTransporterInterface
    template <typename Data, int scheme = goby::middleware::scheme<Data>()>
    void publish_dynamic(const Data& data, const Group& group = Group(),
                         const Publisher<Data>& publisher = Publisher<Data>())
    {
        static_assert(scheme == MarshallingScheme::DCCL,
                      "Can only use DCCL messages with InterVehicleTransporters");

        Data data_with_group = data;
        publisher.set_group(data_with_group, group);

        static_cast<Derived*>(this)->template _publish<Data>(data_with_group, group, publisher);
        inner_.template publish_dynamic<Data, MarshallingScheme::PROTOBUF>(data_with_group, group,
                                                                           publisher);
    }

    template <typename Data, int scheme = goby::middleware::scheme<Data>()>
    void publish_dynamic(std::shared_ptr<const Data> data, const Group& group = Group(),
                         const Publisher<Data>& publisher = Publisher<Data>())
    {
        static_assert(scheme == MarshallingScheme::DCCL,
                      "Can only use DCCL messages with InterVehicleTransporters");
        if (data)
        {
            std::shared_ptr<Data> data_with_group(new Data(*data));
            publisher.set_group(*data_with_group, group);

            static_cast<Derived*>(this)->template _publish<Data>(*data_with_group, group,
                                                                 publisher);
            inner_.template publish_dynamic<Data, MarshallingScheme::PROTOBUF>(data_with_group,
                                                                               group, publisher);
        }
    }

    template <typename Data, int scheme = goby::middleware::scheme<Data>()>
    void publish_dynamic(std::shared_ptr<Data> data, const Group& group = Group(),
                         const Publisher<Data>& publisher = Publisher<Data>())
    {
        publish_dynamic<Data>(std::shared_ptr<const Data>(data), group, publisher);
    }

    template <typename Data, int scheme = goby::middleware::scheme<Data>()>
    void subscribe_dynamic(std::function<void(const Data&)> func, const Group& group = Group(),
                           const Subscriber<Data>& subscriber = Subscriber<Data>())
    {
        static_assert(scheme == MarshallingScheme::DCCL,
                      "Can only use DCCL messages with InterVehicleTransporters");
        auto pointer_ref_lambda = [=](std::shared_ptr<const Data> d) { func(*d); };
        static_cast<Derived*>(this)->template _subscribe<Data>(pointer_ref_lambda, group,
                                                               subscriber);
    }

    template <typename Data, int scheme = goby::middleware::scheme<Data>()>
    void subscribe_dynamic(std::function<void(std::shared_ptr<const Data>)> func,
                           const Group& group = Group(),
                           const Subscriber<Data>& subscriber = Subscriber<Data>())
    {
        static_assert(scheme == MarshallingScheme::DCCL,
                      "Can only use DCCL messages with InterVehicleTransporters");
        static_cast<Derived*>(this)->template _subscribe<Data>(func, group, subscriber);
    }

    std::unique_ptr<InnerTransporter> own_inner_;
    InnerTransporter& inner_;
    static constexpr Group forward_group_{"goby::InterVehicleTransporter"};

  protected:
    template <typename Data>
    std::shared_ptr<protobuf::SerializerTransporterMessage>
    _serialize_publication(const Data& d, const Group& group, const Publisher<Data>& publisher)
    {
        std::vector<char> bytes(
            SerializerParserHelper<Data, MarshallingScheme::DCCL>::serialize(d));
        std::string* sbytes = new std::string(bytes.begin(), bytes.end());
        auto msg = std::make_shared<protobuf::SerializerTransporterMessage>();

        auto* key = msg->mutable_key();
        key->set_marshalling_scheme(MarshallingScheme::DCCL);
        key->set_type(SerializerParserHelper<Data, MarshallingScheme::DCCL>::type_name());
        key->set_group(std::string(group));
        key->set_group_numeric(group.numeric());
        *key->mutable_cfg() = publisher.transport_cfg();

        msg->set_allocated_data(sbytes);
        return msg;
    }

    template <typename Data>
    std::shared_ptr<intervehicle::protobuf::Subscription>
    _serialize_subscription(const Group& group, const Subscriber<Data>& subscriber)
    {
        auto dccl_id = SerializerParserHelper<Data, MarshallingScheme::DCCL>::id();
        auto dccl_subscription = std::make_shared<intervehicle::protobuf::Subscription>();
        dccl_subscription->mutable_header()->set_src(0);

        for (auto id : subscriber.transport_cfg().intervehicle().publisher_id())
            dccl_subscription->mutable_header()->add_dest(id);

        dccl_subscription->set_dccl_id(dccl_id);
        dccl_subscription->set_group(group.numeric());
        dccl_subscription->set_protobuf_name(
            SerializerParserHelper<Data, MarshallingScheme::DCCL>::type_name());
        _insert_file_desc_with_dependencies(Data::descriptor()->file(), dccl_subscription.get());
        *dccl_subscription->mutable_intervehicle() = subscriber.transport_cfg().intervehicle();
        return dccl_subscription;
    }

  private:
    friend PollerType;
    int _poll(std::unique_ptr<std::unique_lock<std::timed_mutex> >& lock)
    {
        return static_cast<Derived*>(this)->_poll(lock);
    }

    // used to populated InterVehicleSubscription file_descriptor fields
    void _insert_file_desc_with_dependencies(const google::protobuf::FileDescriptor* file_desc,
                                             intervehicle::protobuf::Subscription* subscription)
    {
        for (int i = 0, n = file_desc->dependency_count(); i < n; ++i)
            _insert_file_desc_with_dependencies(file_desc->dependency(i), subscription);

        google::protobuf::FileDescriptorProto* file_desc_proto =
            subscription->add_file_descriptor();
        file_desc->CopyTo(file_desc_proto);
    }
};

template <typename Derived, typename InnerTransporter>
constexpr goby::middleware::Group
    InterVehicleTransporterBase<Derived, InnerTransporter>::forward_group_;

template <typename InnerTransporter>
class InterVehicleForwarder
    : public InterVehicleTransporterBase<InterVehicleForwarder<InnerTransporter>, InnerTransporter>
{
  public:
    using Base =
        InterVehicleTransporterBase<InterVehicleForwarder<InnerTransporter>, InnerTransporter>;

    InterVehicleForwarder(InnerTransporter& inner) : Base(inner)
    {
        Base::inner_
            .template subscribe<Base::forward_group_, intervehicle::protobuf::DCCLForwardedData>(
                [this](const intervehicle::protobuf::DCCLForwardedData& d) {
                    _receive_dccl_data_forwarded(d);
                });
    }

    virtual ~InterVehicleForwarder() = default;

    friend Base;

  private:
    template <typename Data>
    void _publish(const Data& d, const Group& group, const Publisher<Data>& publisher)
    {
        // create and forward publication to edge
        auto data = this->_serialize_publication(d, group, publisher);
        Base::inner_.template publish<Base::forward_group_>(data);
    }

    template <typename Data>
    void _subscribe(std::function<void(std::shared_ptr<const Data> d)> func, const Group& group,
                    const Subscriber<Data>& subscriber)
    {
        auto dccl_id = SerializerParserHelper<Data, MarshallingScheme::DCCL>::id();

        auto subscribe_lambda = [=](std::shared_ptr<const Data> d) { func(d); };
        typename SerializationSubscription<Data, MarshallingScheme::DCCL>::HandlerType
            subscribe_function(subscribe_lambda);
        auto subscription = std::shared_ptr<SerializationHandlerBase<> >(
            new SerializationSubscription<Data, MarshallingScheme::DCCL>(subscribe_function, group,
                                                                         subscriber));

        subscriptions_[dccl_id].insert(std::make_pair(group, subscription));

        auto dccl_subscription = this->template _serialize_subscription<Data>(group, subscriber);
        Base::inner_.template publish<Base::forward_group_, intervehicle::protobuf::Subscription,
                                      MarshallingScheme::PROTOBUF>(dccl_subscription);
    }

    int _poll(std::unique_ptr<std::unique_lock<std::timed_mutex> >& lock) { return 0; }

    void _receive_dccl_data_forwarded(const intervehicle::protobuf::DCCLForwardedData& packets)
    {
        for (const auto& packet : packets.frame())
        {
            for (auto p : subscriptions_[packet.dccl_id()])
                p.second->post(packet.data().begin(), packet.data().end());
        }
    }

  private:
    std::unordered_map<int, std::unordered_multimap<
                                std::string, std::shared_ptr<const SerializationHandlerBase<> > > >
        subscriptions_;
};

template <typename InnerTransporter>
class InterVehiclePortal
    : public InterVehicleTransporterBase<InterVehiclePortal<InnerTransporter>, InnerTransporter>
{
    static_assert(
        std::is_same<typename InnerTransporter::InnerTransporterType,
                     InterThreadTransporter>::value,
        "Currently InterVehiclePortal is implemented in a way that requires that its "
        "InnerTransporter's InnerTransporter be goby::middleware::InterThreadTransporter, that is "
        "InterVehicle<InterProcess<goby::middleware::InterThreadTransport>>");

  public:
    using Base =
        InterVehicleTransporterBase<InterVehiclePortal<InnerTransporter>, InnerTransporter>;

    InterVehiclePortal(const intervehicle::protobuf::PortalConfig& cfg) : cfg_(cfg) { _init(); }
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
        auto data = this->_serialize_publication(d, group, publisher);

        if (publisher.transport_cfg().intervehicle().buffer().ack_required() &&
            publisher.acked_func())
        {
            using goby::acomms::protobuf::ModemTransmission;
            auto acked_lambda = [=](std::shared_ptr<const Data> d,
                                    const ModemTransmission& ack_msg) {
                publisher.acked(*d, ack_msg);
            };

            typename PublisherCallback<Data, MarshallingScheme::DCCL,
                                       ModemTransmission>::HandlerType acked_function(acked_lambda);

            auto ack_handler = std::shared_ptr<SerializationHandlerBase<ModemTransmission> >(
                new PublisherCallback<Data, MarshallingScheme::DCCL, ModemTransmission>(
                    acked_function));

            goby::glog.is_debug1() &&
                goby::glog << "Inserting ack handler for id: "
                           << SerializerParserHelper<Data, MarshallingScheme::DCCL>::id()
                           << ", group: " << static_cast<int>(group.numeric()) << std::endl;

            pending_ack_.insert(std::make_pair(*data, ack_handler));
        }

        Base::inner_.inner().template publish<intervehicle::groups::modem_data_out>(data);
    }

    template <typename Data>
    void _subscribe(std::function<void(std::shared_ptr<const Data> d)> func, const Group& group,
                    const Subscriber<Data>& subscriber)
    {
        auto dccl_id = SerializerParserHelper<Data, MarshallingScheme::DCCL>::id();

        auto subscribe_lambda = [=](std::shared_ptr<const Data> d) { func(d); };
        typename SerializationSubscription<Data, MarshallingScheme::DCCL>::HandlerType
            subscribe_function(subscribe_lambda);
        auto subscription = std::shared_ptr<SerializationHandlerBase<> >(
            new SerializationSubscription<Data, MarshallingScheme::DCCL>(subscribe_function, group,
                                                                         subscriber));

        subscriptions_[dccl_id].insert(std::make_pair(group, subscription));

        auto dccl_subscription = this->template _serialize_subscription<Data>(group, subscriber);

        Base::inner_.inner().template publish<intervehicle::groups::modem_subscription_forward_tx>(
            dccl_subscription);
    }

    int _poll(std::unique_ptr<std::unique_lock<std::timed_mutex> >& lock)
    {
        int items = 0;
        goby::acomms::protobuf::ModemTransmission msg;
        while (!received_.empty())
        {
            _receive(received_.front());
            received_.pop_front();
            ++items;
            if (lock)
                lock.reset();
        }
        return items;
    }

    void _init()
    {
        Base::inner_
            .template subscribe<Base::forward_group_, protobuf::SerializerTransporterMessage>(
                [this](const protobuf::SerializerTransporterMessage& msg) {
                    Base::inner_.inner().template publish<intervehicle::groups::modem_data_out>(
                        msg);
                });

        Base::inner_.template subscribe<Base::forward_group_, intervehicle::protobuf::Subscription,
                                        MarshallingScheme::PROTOBUF>(
            [this](const intervehicle::protobuf::Subscription& d) {
                auto group = d.group();
                forwarded_subscriptions_[d.dccl_id()].insert(std::make_pair(group, d));
                DCCLSerializerParserHelperBase::load_forwarded_subscription(d);

                goby::glog.is_debug1() && goby::glog << "Received subscription from local Forwarder"
                                                     << std::endl;

                Base::inner_.inner()
                    .template publish<intervehicle::groups::modem_subscription_forward_tx>(d);
            });

        // set up reception of forwarded (via acoustic) subscriptions,
        // then re-publish to driver threads
        {
            using intervehicle::protobuf::Subscription;
            auto subscribe_lambda = [=](std::shared_ptr<const Subscription> d) {
                Base::inner_.inner()
                    .template publish<intervehicle::groups::modem_subscription_forward_rx>(d);
            };

            typename SerializationSubscription<Subscription, MarshallingScheme::DCCL>::HandlerType
                subscribe_function(subscribe_lambda);
            auto subscription = std::shared_ptr<SerializationHandlerBase<> >(
                new SerializationSubscription<Subscription, MarshallingScheme::DCCL>(
                    subscribe_function));

            auto dccl_id = SerializerParserHelper<Subscription, MarshallingScheme::DCCL>::id();
            subscriptions_[dccl_id].insert(
                std::make_pair(subscription->subscribed_group(), subscription));
        }

        Base::inner_.inner()
            .template subscribe<intervehicle::groups::modem_data_in,
                                goby::acomms::protobuf::ModemTransmission>(
                [this](const goby::acomms::protobuf::ModemTransmission& msg) {
                    received_.push_back(msg);
                });

        {
            using ack_pair_type = std::pair<protobuf::SerializerTransporterMessage,
                                            goby::acomms::protobuf::ModemTransmission>;

            Base::inner_.inner()
                .template subscribe<intervehicle::groups::modem_ack_in, ack_pair_type>(
                    [this](const ack_pair_type& ack_pair) {
                        const auto& original = ack_pair.first;
                        const auto& ack_msg = ack_pair.second;
                        auto it = pending_ack_.find(original);
                        if (it != pending_ack_.end())
                        {
                            it->second->post(original.data().begin(), original.data().end(),
                                             ack_msg);
                            pending_ack_.erase(it);
                        }
                    });
        }

        Base::inner_.inner().template subscribe<intervehicle::groups::modem_driver_ready, bool>(
            [this](const bool& ready) {
                goby::glog.is_debug1() && goby::glog << "Received driver ready" << std::endl;
                ++drivers_ready_;
            });

        for (const auto& lib_path : cfg_.dccl_load_library())
            DCCLSerializerParserHelperBase::load_library(lib_path);

        for (int i = 0, n = cfg_.link_size(); i < n; ++i)
        {
            auto* link = cfg_.mutable_link(i);

            link->mutable_driver()->set_modem_id(link->modem_id());
            link->mutable_mac()->set_modem_id(link->modem_id());

            modem_drivers_.emplace_back(new ModemDriverData);
            ModemDriverData& data = *modem_drivers_.back();

            data.underlying_thread.reset(new std::thread([&data, link]() {
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
                }
            }));
        }

        while (drivers_ready_ < modem_drivers_.size())
        {
            goby::glog.is_debug1() && goby::glog << "Waiting for drivers to be ready." << std::endl;
            this->poll();
        }
    }

    void _receive(const goby::acomms::protobuf::ModemTransmission& rx_msg)
    {
        if (rx_msg.type() == goby::acomms::protobuf::ModemTransmission::ACK) {}
        else
        {
            for (auto& frame : rx_msg.frame())
            {
                const intervehicle::protobuf::DCCLForwardedData packets(
                    DCCLSerializerParserHelperBase::unpack(frame));
                for (const auto& packet : packets.frame())
                {
                    for (auto p : subscriptions_[packet.dccl_id()])
                        p.second->post(packet.data().begin(), packet.data().end());
                }

                // send back to Forwarders
                Base::inner_.template publish<Base::forward_group_>(packets);
            }
        }
    }

    intervehicle::protobuf::PortalConfig cfg_;

    struct ModemDriverData
    {
        std::unique_ptr<std::thread> underlying_thread;
        std::unique_ptr<intervehicle::ModemDriverThread> modem_driver_thread;
        std::atomic<bool> driver_thread_alive{true};
    };
    std::vector<std::unique_ptr<ModemDriverData> > modem_drivers_;
    int drivers_ready_{0};

    std::deque<goby::acomms::protobuf::ModemTransmission> received_;

    // maps DCCL ID to map of Group->subscription
    std::unordered_map<int, std::unordered_multimap<
                                std::string, std::shared_ptr<const SerializationHandlerBase<> > > >
        subscriptions_;

    std::unordered_map<int, std::unordered_multimap<Group, intervehicle::protobuf::Subscription> >
        forwarded_subscriptions_;

    std::unordered_map<
        protobuf::SerializerTransporterMessage,
        std::shared_ptr<SerializationHandlerBase<goby::acomms::protobuf::ModemTransmission> > >
        pending_ack_;
};
} // namespace middleware
} // namespace goby

#endif
