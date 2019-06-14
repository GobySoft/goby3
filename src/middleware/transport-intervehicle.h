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

#include "goby/acomms/amac.h"
#include "goby/acomms/bind.h"
#include "goby/acomms/modem_driver.h"
#include "goby/acomms/queue.h"

#include "goby/acomms/modemdriver/iridium_driver.h"
#include "goby/acomms/modemdriver/iridium_shore_driver.h"
#include "goby/acomms/modemdriver/udp_driver.h"

#include "goby/middleware/protobuf/intervehicle_config.pb.h"
#include "transport-common.h"

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

    // publish without a group
    template <typename Data>
    void publish_no_group(const Data& data,
                          const goby::middleware::protobuf::TransporterConfig& transport_cfg =
                              goby::middleware::protobuf::TransporterConfig())
    {
        publish_dynamic<Data>(data, Group(), transport_cfg);
    }

    template <typename Data>
    void publish_no_group(std::shared_ptr<const Data> data,
                          const goby::middleware::protobuf::TransporterConfig& transport_cfg =
                              goby::middleware::protobuf::TransporterConfig())
    {
        publish_dynamic<Data>(data, Group(), transport_cfg);
    }

    template <typename Data>
    void publish_no_group(std::shared_ptr<Data> data,
                          const goby::middleware::protobuf::TransporterConfig& transport_cfg =
                              goby::middleware::protobuf::TransporterConfig())
    {
        publish_no_group<Data>(std::shared_ptr<const Data>(data), transport_cfg);
    }

    //subscribe without a group
    template <typename Data>
    void subscribe_no_group(std::function<void(const Data&)> func,
                            std::function<Group(const Data&)> group_func = [](const Data&) {
                                return Group();
                            })
    {
        subscribe_dynamic<Data>(func, Group(), group_func);
    }

    template <typename Data>
    void subscribe_no_group(std::function<void(std::shared_ptr<const Data>)> func,
                            std::function<Group(const Data&)> group_func = [](const Data&) {
                                return Group();
                            })
    {
        subscribe_dynamic<Data>(func, Group(), group_func);
    }

    // implements StaticTransporterInteface
    template <typename Data, int scheme = scheme<Data>()>
    void publish_dynamic(const Data& data, const Group& group = Group(),
                         const goby::middleware::protobuf::TransporterConfig& transport_cfg =
                             goby::middleware::protobuf::TransporterConfig())
    {
        static_assert(scheme == MarshallingScheme::DCCL,
                      "Can only use DCCL messages with InterVehicleTransporters");
        static_cast<Derived*>(this)->template _publish<Data>(data, group, transport_cfg);
        inner_.template publish_dynamic<Data, MarshallingScheme::PROTOBUF>(data, group,
                                                                           transport_cfg);
    }

    template <typename Data, int scheme = scheme<Data>()>
    void publish_dynamic(std::shared_ptr<const Data> data, const Group& group = Group(),
                         const goby::middleware::protobuf::TransporterConfig& transport_cfg =
                             goby::middleware::protobuf::TransporterConfig())
    {
        static_assert(scheme == MarshallingScheme::DCCL,
                      "Can only use DCCL messages with InterVehicleTransporters");
        if (data)
        {
            static_cast<Derived*>(this)->template _publish<Data>(*data, group, transport_cfg);
            inner_.template publish_dynamic<Data, MarshallingScheme::PROTOBUF>(data, group,
                                                                               transport_cfg);
        }
    }

    template <typename Data, int scheme = scheme<Data>()>
    void publish_dynamic(std::shared_ptr<Data> data, const Group& group = Group(),
                         const goby::middleware::protobuf::TransporterConfig& transport_cfg =
                             goby::middleware::protobuf::TransporterConfig())
    {
        publish_dynamic<Data>(std::shared_ptr<const Data>(data), group, transport_cfg);
    }

    template <typename Data, int scheme = scheme<Data>()>
    void subscribe_dynamic(std::function<void(const Data&)> func, const Group& group = Group(),
                           std::function<Group(const Data&)> group_func = [](const Data&) {
                               return Group();
                           })
    {
        static_assert(scheme == MarshallingScheme::DCCL,
                      "Can only use DCCL messages with InterVehicleTransporters");
        auto pointer_ref_lambda = [=](std::shared_ptr<const Data> d) { func(*d); };
        static_cast<Derived*>(this)->template _subscribe<Data>(pointer_ref_lambda, group,
                                                               group_func);
    }

    template <typename Data, int scheme = scheme<Data>()>
    void subscribe_dynamic(std::function<void(std::shared_ptr<const Data>)> func,
                           const Group& group = Group(),
                           std::function<Group(const Data&)> group_func = [](const Data&) {
                               return Group();
                           })
    {
        static_assert(scheme == MarshallingScheme::DCCL,
                      "Can only use DCCL messages with InterVehicleTransporters");
        static_cast<Derived*>(this)->template _subscribe<Data>(func, group, group_func);
    }

    std::unique_ptr<InnerTransporter> own_inner_;
    InnerTransporter& inner_;
    static constexpr Group forward_group_{"goby::InterVehicleTransporter"};

  private:
    friend PollerType;
    int _poll(std::unique_ptr<std::unique_lock<std::timed_mutex> >& lock)
    {
        return static_cast<Derived*>(this)->_poll(lock);
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
        Base::inner_.template subscribe<Base::forward_group_,
                                        goby::middleware::protobuf::DCCLForwardedData>(
            [this](const goby::middleware::protobuf::DCCLForwardedData& d) {
                _receive_dccl_data_forwarded(d);
            });
    }

    virtual ~InterVehicleForwarder() = default;

    friend Base;

  private:
    template <typename Data>
    void _publish(const Data& d, const Group& group,
                  const goby::middleware::protobuf::TransporterConfig& transport_cfg)
    {
        // create and forward publication to edge
        std::vector<char> bytes(
            SerializerParserHelper<Data, MarshallingScheme::DCCL>::serialize(d));
        std::string* sbytes = new std::string(bytes.begin(), bytes.end());
        std::shared_ptr<goby::middleware::protobuf::SerializerTransporterData> data =
            std::make_shared<goby::middleware::protobuf::SerializerTransporterData>();

        data->set_marshalling_scheme(MarshallingScheme::DCCL);
        data->set_type(SerializerParserHelper<Data, MarshallingScheme::DCCL>::type_name());
        data->set_group(std::string(group));
        data->set_allocated_data(sbytes);

        *data->mutable_cfg() = transport_cfg;

        Base::inner_.template publish<Base::forward_group_>(data);
    }

    template <typename Data>
    void _subscribe(std::function<void(std::shared_ptr<const Data> d)> func, const Group& group,
                    std::function<Group(const Data&)> group_func)
    {
        auto dccl_id = SerializerParserHelper<Data, MarshallingScheme::DCCL>::id();

        auto subscribe_lambda = [=](std::shared_ptr<const Data> d,
                                    const goby::middleware::protobuf::TransporterConfig& t) {
            func(d);
        };
        typename SerializationSubscription<Data, MarshallingScheme::DCCL>::HandlerType
            subscribe_function(subscribe_lambda);
        auto subscription = std::shared_ptr<SerializationSubscriptionBase>(
            new SerializationSubscription<Data, MarshallingScheme::DCCL>(subscribe_function, group,
                                                                         group_func));

        subscriptions_[dccl_id].insert(std::make_pair(group, subscription));

        goby::middleware::protobuf::DCCLSubscription dccl_subscription;
        dccl_subscription.set_dccl_id(dccl_id);
        dccl_subscription.set_group(group.numeric());
        dccl_subscription.set_protobuf_name(
            SerializerParserHelper<Data, MarshallingScheme::DCCL>::type_name());
        _insert_file_desc_with_dependencies(Data::descriptor()->file(), &dccl_subscription);
        Base::inner_
            .template publish<Base::forward_group_, goby::middleware::protobuf::DCCLSubscription>(
                dccl_subscription);
    }

    // used to populated DCCLSubscription file_descriptor fields
    void _insert_file_desc_with_dependencies(const google::protobuf::FileDescriptor* file_desc,
                                             protobuf::DCCLSubscription* subscription)
    {
        for (int i = 0, n = file_desc->dependency_count(); i < n; ++i)
            _insert_file_desc_with_dependencies(file_desc->dependency(i), subscription);

        google::protobuf::FileDescriptorProto* file_desc_proto =
            subscription->add_file_descriptor();
        file_desc->CopyTo(file_desc_proto);
    }

    int _poll(std::unique_ptr<std::unique_lock<std::timed_mutex> >& lock) { return 0; }

    void _receive_dccl_data_forwarded(const goby::middleware::protobuf::DCCLForwardedData& packets)
    {
        for (const auto& packet : packets.frame())
        {
            for (auto p : subscriptions_[packet.dccl_id()])
                p.second->post(packet.data().begin(), packet.data().end());
        }
    }

  private:
    std::unordered_map<
        int, std::unordered_multimap<std::string,
                                     std::shared_ptr<const SerializationSubscriptionBase> > >
        subscriptions_;
};

class ModemDriverThread
{
  public:
    ModemDriverThread(const protobuf::InterVehiclePortalConfig& cfg, std::atomic<bool>& alive,
                      std::shared_ptr<std::condition_variable_any> poller_cv);
    void run();
    void publish(const std::string& bytes);
    bool retrieve_message(goby::acomms::protobuf::ModemTransmission* msg);
    int tx_queue_size()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return sending_.size();
    }

  private:
    void _receive(const goby::acomms::protobuf::ModemTransmission& rx_msg);
    void _data_request(goby::acomms::protobuf::ModemTransmission* msg);

  private:
    std::mutex mutex_;
    const protobuf::InterVehiclePortalConfig& cfg_;
    std::atomic<bool>& alive_;
    std::shared_ptr<std::condition_variable_any> poller_cv_;

    std::deque<std::string> sending_;
    std::deque<goby::acomms::protobuf::ModemTransmission> received_;

    // goby::acomms::QueueManager q_manager_;

    // for UDPDriver
    std::vector<std::unique_ptr<boost::asio::io_service> > asio_service_;
    std::unique_ptr<goby::acomms::ModemDriverBase> driver_;

    goby::acomms::MACManager mac_;
};

template <typename InnerTransporter = NullTransporter>
class InterVehiclePortal
    : public InterVehicleTransporterBase<InterVehiclePortal<InnerTransporter>, InnerTransporter>
{
  public:
    using Base =
        InterVehicleTransporterBase<InterVehiclePortal<InnerTransporter>, InnerTransporter>;

    InterVehiclePortal(const protobuf::InterVehiclePortalConfig& cfg)
        : cfg_(cfg), driver_thread_(cfg, driver_thread_alive_, PollerInterface::cv())
    {
        _init();
    }
    InterVehiclePortal(InnerTransporter& inner, const protobuf::InterVehiclePortalConfig& cfg)
        : Base(inner), cfg_(cfg), driver_thread_(cfg, driver_thread_alive_, PollerInterface::cv())
    {
        _init();
    }

    ~InterVehiclePortal()
    {
        driver_thread_alive_ = false;
        if (modem_driver_thread_)
            modem_driver_thread_->join();
    }

    int tx_queue_size() { return driver_thread_.tx_queue_size(); }

    friend Base;

  private:
    template <typename Data>
    void _publish(const Data& data, const Group& group,
                  const goby::middleware::protobuf::TransporterConfig& transport_cfg)
    {
        std::vector<char> bytes(
            SerializerParserHelper<Data, MarshallingScheme::DCCL>::serialize(data));
        driver_thread_.publish(std::string(bytes.begin(), bytes.end()));
    }

    template <typename Data>
    void _subscribe(std::function<void(std::shared_ptr<const Data> d)> func, const Group& group,
                    std::function<Group(const Data&)> group_func)
    {
        auto dccl_id = SerializerParserHelper<Data, MarshallingScheme::DCCL>::id();

        auto subscribe_lambda = [=](std::shared_ptr<const Data> d,
                                    const goby::middleware::protobuf::TransporterConfig& t) {
            func(d);
        };
        typename SerializationSubscription<Data, MarshallingScheme::DCCL>::HandlerType
            subscribe_function(subscribe_lambda);
        auto subscription = std::shared_ptr<SerializationSubscriptionBase>(
            new SerializationSubscription<Data, MarshallingScheme::DCCL>(subscribe_function, group,
                                                                         group_func));

        subscriptions_[dccl_id].insert(std::make_pair(group, subscription));
    }

    int _poll(std::unique_ptr<std::unique_lock<std::timed_mutex> >& lock)
    {
        int items = 0;
        goby::acomms::protobuf::ModemTransmission msg;
        while (driver_thread_.retrieve_message(&msg))
        {
            _receive(msg);
            ++items;
            if (lock)
                lock.reset();
        }
        return items;
    }

    void _init()
    {
        Base::inner_.template subscribe<Base::forward_group_,
                                        goby::middleware::protobuf::SerializerTransporterData>(
            [this](const goby::middleware::protobuf::SerializerTransporterData& d) {
                _receive_publication_forwarded(d);
            });
        Base::inner_
            .template subscribe<Base::forward_group_, goby::middleware::protobuf::DCCLSubscription>(
                [this](const goby::middleware::protobuf::DCCLSubscription& d) {
                    _receive_subscription_forwarded(d);
                });
        modem_driver_thread_.reset(new std::thread([this]() { driver_thread_.run(); }));
    }

    void _receive(const goby::acomms::protobuf::ModemTransmission& rx_msg)
    {
        for (auto& frame : rx_msg.frame())
        {
            const protobuf::DCCLForwardedData packets(
                DCCLSerializerParserHelperBase::unpack(frame));
            for (const auto& packet : packets.frame())
            {
                for (auto p : subscriptions_[packet.dccl_id()])
                    p.second->post(packet.data().begin(), packet.data().end());
            }

            // forward to edges
            Base::inner_.template publish<Base::forward_group_>(packets);
        }
    }

    void _receive_publication_forwarded(
        const goby::middleware::protobuf::SerializerTransporterData& data)
    {
        driver_thread_.publish(data.data());
    }

    void _receive_subscription_forwarded(
        const goby::middleware::protobuf::DCCLSubscription& dccl_subscription)
    {
        auto group = dccl_subscription.group();
        forwarded_subscriptions_[dccl_subscription.dccl_id()].insert(
            std::make_pair(group, dccl_subscription));
        DCCLSerializerParserHelperBase::load_forwarded_subscription(dccl_subscription);
    }

    const goby::middleware::protobuf::InterVehiclePortalConfig& cfg_;
    std::unique_ptr<std::thread> modem_driver_thread_;
    std::atomic<bool> driver_thread_alive_{true};
    ModemDriverThread driver_thread_;

    // maps DCCL ID to map of Group->subscription
    std::unordered_map<
        int, std::unordered_multimap<std::string,
                                     std::shared_ptr<const SerializationSubscriptionBase> > >
        subscriptions_;
    std::unordered_map<
        int, std::unordered_multimap<Group, goby::middleware::protobuf::DCCLSubscription> >
        forwarded_subscriptions_;
};
} // namespace middleware
} // namespace goby

#endif
