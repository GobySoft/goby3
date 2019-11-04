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
#include "goby/middleware/transport/common.h"
#include "goby/middleware/transport/interthread.h" // used for InterVehiclePortal implementation

#include "goby/middleware/intervehicle/driver-thread.h"

namespace goby
{
namespace middleware
{
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
    InterVehicleTransporterBase(InnerTransporter& inner)
        : InterfaceType(inner), PollerType(&this->inner())
    {
    }
    InterVehicleTransporterBase() : PollerType(&this->inner()) {}

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
        this->inner().template publish_dynamic<Data, MarshallingScheme::PROTOBUF>(data_with_group,
                                                                                  group, publisher);
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
            this->inner().template publish_dynamic<Data, MarshallingScheme::PROTOBUF>(
                data_with_group, group, publisher);
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

  protected:
    template <typename Data>
    std::shared_ptr<goby::middleware::protobuf::SerializerTransporterMessage>
    _set_up_publish(const Data& d, const Group& group, const Publisher<Data>& publisher)
    {
        auto data = intervehicle::serialize_publication(d, group, publisher);

        if (publisher.cfg().intervehicle().buffer().ack_required())
        {
            auto ack_handler = std::make_shared<
                PublisherCallback<Data, MarshallingScheme::DCCL, intervehicle::protobuf::AckData>>(
                publisher.acked_func());

            auto expire_handler =
                std::make_shared<PublisherCallback<Data, MarshallingScheme::DCCL,
                                                   intervehicle::protobuf::ExpireData>>(
                    publisher.expired_func());

            this->_insert_pending_ack(SerializerParserHelper<Data, MarshallingScheme::DCCL>::id(),
                                      data, ack_handler, expire_handler);
        }

        return data;
    }

    template <typename Data>
    std::shared_ptr<intervehicle::protobuf::Subscription>
    _set_up_subscribe(std::function<void(std::shared_ptr<const Data> d)> func, const Group& group,
                      const Subscriber<Data>& subscriber)
    {
        auto dccl_id = SerializerParserHelper<Data, MarshallingScheme::DCCL>::id();

        auto subscription =
            std::make_shared<SerializationSubscription<Data, MarshallingScheme::DCCL>>(func, group,
                                                                                       subscriber);

        this->subscriptions_[dccl_id].insert(std::make_pair(group, subscription));

        auto dccl_subscription = this->template _serialize_subscription<Data>(group, subscriber);
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
        for (const auto& packet : packets.frame())
        {
            for (auto p : this->subscriptions_[packet.dccl_id()])
                p.second->post(packet.data().begin(), packet.data().end());
        }
    }

    template <typename Data>
    std::shared_ptr<intervehicle::protobuf::Subscription>
    _serialize_subscription(const Group& group, const Subscriber<Data>& subscriber)
    {
        auto dccl_id = SerializerParserHelper<Data, MarshallingScheme::DCCL>::id();
        auto dccl_subscription = std::make_shared<intervehicle::protobuf::Subscription>();
        dccl_subscription->mutable_header()->set_src(0);

        for (auto id : subscriber.cfg().intervehicle().publisher_id())
            dccl_subscription->mutable_header()->add_dest(id);

        dccl_subscription->set_dccl_id(dccl_id);
        dccl_subscription->set_group(group.numeric());
        dccl_subscription->set_time_with_units(
            goby::time::SystemClock::now<goby::time::MicroTime>());
        dccl_subscription->set_protobuf_name(
            SerializerParserHelper<Data, MarshallingScheme::DCCL>::type_name());
        _insert_file_desc_with_dependencies(Data::descriptor()->file(), dccl_subscription.get());
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
    std::unordered_map<int, std::unordered_multimap<
                                std::string, std::shared_ptr<const SerializationHandlerBase<>>>>
        subscriptions_;

  private:
    friend PollerType;
    int _poll(std::unique_ptr<std::unique_lock<std::timed_mutex>>& lock)
    {
        _expire_pending_ack();

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
                                  acomms::protobuf::DynamicBufferConfig::ttl_unit());

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
};

template <typename InnerTransporter>
class InterVehicleForwarder
    : public InterVehicleTransporterBase<InterVehicleForwarder<InnerTransporter>, InnerTransporter>
{
  public:
    using Base =
        InterVehicleTransporterBase<InterVehicleForwarder<InnerTransporter>, InnerTransporter>;

    InterVehicleForwarder(InnerTransporter& inner) : Base(inner)
    {
        this->inner()
            .template subscribe<intervehicle::groups::modem_data_in,
                                intervehicle::protobuf::DCCLForwardedData>(
                [this](const intervehicle::protobuf::DCCLForwardedData& msg) {
                    this->_receive(msg);
                });

        using ack_pair_type = intervehicle::protobuf::AckMessagePair;
        this->inner().template subscribe<intervehicle::groups::modem_ack_in, ack_pair_type>(
            [this](const ack_pair_type& ack_pair) {
                this->template _handle_ack_or_expire<0>(ack_pair);
            });

        using expire_pair_type = intervehicle::protobuf::ExpireMessagePair;
        this->inner().template subscribe<intervehicle::groups::modem_expire_in, expire_pair_type>(
            [this](const expire_pair_type& expire_pair) {
                this->template _handle_ack_or_expire<1>(expire_pair);
            });
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
                    const Subscriber<Data>& subscriber)
    {
        this->inner()
            .template publish<intervehicle::groups::modem_subscription_forward_tx,
                              intervehicle::protobuf::Subscription, MarshallingScheme::PROTOBUF>(
                this->_set_up_subscribe(func, group, subscriber));
    }

    int _poll(std::unique_ptr<std::unique_lock<std::timed_mutex>>& lock) { return 0; }
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
        this->inner().inner().template publish<intervehicle::groups::modem_data_out>(
            this->_set_up_publish(d, group, publisher));
    }

    template <typename Data>
    void _subscribe(std::function<void(std::shared_ptr<const Data> d)> func, const Group& group,
                    const Subscriber<Data>& subscriber)
    {
        auto dccl_subscription = this->_set_up_subscribe(func, group, subscriber);

        this->inner().inner().template publish<intervehicle::groups::modem_subscription_forward_tx>(
            dccl_subscription);
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
            auto subscribe_lambda = [=](std::shared_ptr<const Subscription> d) {
                this->inner()
                    .inner()
                    .template publish<intervehicle::groups::modem_subscription_forward_rx,
                                      intervehicle::protobuf::Subscription,
                                      MarshallingScheme::PROTOBUF>(d);
            };
            auto subscription =
                std::make_shared<SerializationSubscription<Subscription, MarshallingScheme::DCCL>>(
                    subscribe_lambda);

            auto dccl_id = SerializerParserHelper<Subscription, MarshallingScheme::DCCL>::id();
            this->subscriptions_[dccl_id].insert(
                std::make_pair(subscription->subscribed_group(), subscription));
        }

        this->inner()
            .inner()
            .template subscribe<intervehicle::groups::modem_data_in,
                                intervehicle::protobuf::DCCLForwardedData>(
                [this](const intervehicle::protobuf::DCCLForwardedData& msg) {
                    received_.push_back(msg);
                });

        // a message required ack can be disposed by either [1] ack, [2] expire (TTL exceeded), [3] having no subscribers, [4] queue size exceeded.
        // post the correct callback (ack for [1] and expire for [2-4])
        // and remove the pending ack message
        using ack_pair_type = intervehicle::protobuf::AckMessagePair;
        this->inner().inner().template subscribe<intervehicle::groups::modem_ack_in, ack_pair_type>(
            [this](const ack_pair_type& ack_pair) {
                this->template _handle_ack_or_expire<0>(ack_pair);
            });

        using expire_pair_type = intervehicle::protobuf::ExpireMessagePair;
        this->inner()
            .inner()
            .template subscribe<intervehicle::groups::modem_expire_in, expire_pair_type>(
                [this](const expire_pair_type& expire_pair) {
                    this->template _handle_ack_or_expire<1>(expire_pair);
                });

        this->inner().inner().template subscribe<intervehicle::groups::modem_driver_ready, bool>(
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
};
} // namespace middleware
} // namespace goby

#endif
