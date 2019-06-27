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

#include "goby/acomms/bind.h"
#include "goby/acomms/modem_driver.h"

#include "driver-thread.h"

using goby::glog;
using namespace goby::util::logger;
using goby::middleware::protobuf::SerializerTransporterMessage;

goby::middleware::intervehicle::ModemDriverThread::ModemDriverThread(
    const intervehicle::protobuf::PortalConfig::LinkConfig& config)
    : goby::middleware::Thread<intervehicle::protobuf::PortalConfig::LinkConfig,
                               InterThreadTransporter>(config, 10 * boost::units::si::hertz)
{
    interthread_.reset(new InterThreadTransporter);
    this->set_transporter(interthread_.get());

    interthread_->subscribe<groups::modem_data_out, SerializerTransporterMessage>(
        [this](std::shared_ptr<const SerializerTransporterMessage> msg) { _buffer_message(msg); });

    interthread_
        ->subscribe<groups::modem_subscription_forward_tx, intervehicle::protobuf::Subscription>(
            [this](std::shared_ptr<const intervehicle::protobuf::Subscription> subscription) {
                _forward_subscription(*subscription);
            });

    interthread_
        ->subscribe<groups::modem_subscription_forward_rx, intervehicle::protobuf::Subscription>(
            [this](std::shared_ptr<const intervehicle::protobuf::Subscription> subscription) {
                _accept_subscription(*subscription);
            });

    if (cfg().driver().has_driver_name())
    {
        throw(goby::Exception("Driver plugins not yet supported by InterVehicle transporters: use "
                              "driver_type enumerations."));
    }
    else
    {
        switch (cfg().driver().driver_type())
        {
            case goby::acomms::protobuf::DRIVER_WHOI_MICROMODEM:
                driver_.reset(new goby::acomms::MMDriver);
                break;

            case goby::acomms::protobuf::DRIVER_IRIDIUM:
                driver_.reset(new goby::acomms::IridiumDriver);
                break;

            case goby::acomms::protobuf::DRIVER_UDP:
                driver_.reset(new goby::acomms::UDPDriver);
                break;

            case goby::acomms::protobuf::DRIVER_UDP_MULTICAST:
                driver_.reset(new goby::acomms::UDPMulticastDriver);
                break;

            case goby::acomms::protobuf::DRIVER_IRIDIUM_SHORE:
                driver_.reset(new goby::acomms::IridiumShoreDriver);
                break;

            case goby::acomms::protobuf::DRIVER_BENTHOS_ATM900:
                driver_.reset(new goby::acomms::BenthosATM900Driver);
                break;

            case goby::acomms::protobuf::DRIVER_NONE:
            case goby::acomms::protobuf::DRIVER_ABC_EXAMPLE_MODEM:
            case goby::acomms::protobuf::DRIVER_UFIELD_SIM_DRIVER:
            case goby::acomms::protobuf::DRIVER_BLUEFIN_MOOS:
                throw(goby::Exception(
                    "Unsupported driver type: " +
                    goby::acomms::protobuf::DriverType_Name(cfg().driver().driver_type())));
                break;
        }
    }

    driver_->signal_receive.connect(
        [&](const goby::acomms::protobuf::ModemTransmission& rx_msg) { _receive(rx_msg); });

    driver_->signal_data_request.connect(
        [&](goby::acomms::protobuf::ModemTransmission* msg) { this->_data_request(msg); });

    goby::acomms::bind(mac_, *driver_);

    mac_.startup(cfg().mac());
    driver_->startup(cfg().driver());

    subscription_key_.set_marshalling_scheme(MarshallingScheme::DCCL);
    subscription_key_.set_type(intervehicle::protobuf::Subscription::descriptor()->full_name());
    subscription_key_.set_group_numeric(Group::broadcast_group);

    goby::glog.is_debug1() && goby::glog << "Driver ready" << std::endl;
    interthread_->publish<groups::modem_driver_ready, bool>(true);
}

void goby::middleware::intervehicle::ModemDriverThread::loop()
{
    // add notification for expired messages
    buffer_.expire();
    driver_->do_work();
    mac_.do_work();
}

void goby::middleware::intervehicle::ModemDriverThread::_forward_subscription(
    intervehicle::protobuf::Subscription subscription)
{
    subscription.mutable_header()->set_src(cfg().driver().modem_id());
    for (auto dest : subscription.header().dest())
    {
        auto buffer_id = _create_buffer_id(subscription_key_);
        if (!subscription_subbuffers_.count(dest))
        {
            buffer_.create(dest, buffer_id, cfg().subscription_buffer());
            subscription_subbuffers_.insert(dest);
        }

        glog.is_debug1() && glog << "Forwarding subscription acoustically: "
                                 << _create_buffer_id(subscription) << std::endl;

        SerializerTransporterMessage subscription_publication;
        auto* key = subscription_publication.mutable_key();
        key->set_marshalling_scheme(MarshallingScheme::DCCL);
        key->set_type(SerializerParserHelper<SerializerTransporterMessage,
                                             MarshallingScheme::DCCL>::type_name());
        key->set_group("");
        key->set_group_numeric(Group::broadcast_group);
        std::vector<char> bytes(
            SerializerParserHelper<intervehicle::protobuf::Subscription,
                                   MarshallingScheme::DCCL>::serialize(subscription));
        std::string* sbytes = new std::string(bytes.begin(), bytes.end());
        subscription_publication.set_allocated_data(sbytes);

        buffer_.push({dest, buffer_id, goby::time::SteadyClock::now(), subscription_publication});
    }
}

void goby::middleware::intervehicle::ModemDriverThread::_data_request(
    goby::acomms::protobuf::ModemTransmission* msg)
{
    // erase any pending acks with greater frame numbers (we never received these)
    auto it = pending_ack_.lower_bound(msg->frame_start()), end = pending_ack_.end();
    while (it != end)
    {
        goby::glog << "Erasing " << it->second.size() << " values not acked for frame " << it->first
                   << std::endl;
        it = pending_ack_.erase(it);
    }

    int dest = msg->dest();
    for (auto frame_number = msg->frame_start(),
              total_frames = msg->max_num_frames() + msg->frame_start();
         frame_number < total_frames; ++frame_number)
    {
        std::string* frame = msg->add_frame();

        while (frame->size() < msg->max_frame_bytes())
        {
            try
            {
                auto buffer_value =
                    buffer_.top(dest, msg->max_frame_bytes() - frame->size(),
                                goby::time::convert_duration<std::chrono::microseconds>(
                                    cfg().ack_timeout_with_units()));
                dest = buffer_value.modem_id;
                *frame += buffer_value.data.data();

                bool ack_required = buffer_.sub(buffer_value.modem_id, buffer_value.subbuffer_id)
                                        .cfg()
                                        .ack_required();

                if (!ack_required)
                {
                    buffer_.erase(buffer_value);
                }
                else
                {
                    msg->set_ack_requested(true);
                    pending_ack_[frame_number].push_back(buffer_value);
                }
            }
            catch (goby::acomms::DynamicBufferNoDataException&)
            {
                break;
            }
        }
    }
    msg->set_dest(dest);
}

goby::middleware::intervehicle::ModemDriverThread::subbuffer_id_type
goby::middleware::intervehicle::ModemDriverThread::_create_buffer_id(unsigned dccl_id,
                                                                     unsigned group)
{
    return "/group:" + std::to_string(group) + "/id:" + std::to_string(dccl_id) + "/";
}

void goby::middleware::intervehicle::ModemDriverThread::_accept_subscription(
    const intervehicle::protobuf::Subscription& subscription)
{
    auto buffer_id = _create_buffer_id(subscription);

    glog.is_debug2() &&
        glog << "Received new forwarded subscription: " << subscription.ShortDebugString()
             << ", buffer_id: " << buffer_id << std::endl;

    auto dest = subscription.header().src();
    // TODO see if there's a change to the buffer configuration with this subscription
    if (!subscriber_buffer_cfg_[dest].count(buffer_id))
    {
        subscriber_buffer_cfg_[dest].insert(std::make_pair(buffer_id, subscription));

        // check if there's a publisher already, if so create the buffer
        auto pub_it = publisher_buffer_cfg_.find(buffer_id);
        if (pub_it != publisher_buffer_cfg_.end())
        {
            _create_buffer(dest, buffer_id,
                           {pub_it->second.cfg().intervehicle().buffer(),
                            subscription.intervehicle().buffer()});
        }
    }
    else
    {
        glog.is_debug2() && glog << "Subscription configuration exists for " << buffer_id
                                 << std::endl;
    }
}

void goby::middleware::intervehicle::ModemDriverThread::_create_buffer(
    modem_id_type dest_id, subbuffer_id_type buffer_id,
    const std::vector<goby::acomms::protobuf::DynamicBufferConfig>& cfgs)
{
    // TODO create broadcast buffers if the configuration is not ack_required

    buffer_.create(dest_id, buffer_id, cfgs);
    subbuffers_created_[buffer_id].insert(dest_id);
    glog.is_debug2() && glog << "Created buffer for dest: " << dest_id << " for id: " << buffer_id
                             << std::endl;
}

void goby::middleware::intervehicle::ModemDriverThread::_buffer_message(
    std::shared_ptr<const SerializerTransporterMessage> msg)
{
    auto buffer_id = _create_buffer_id(msg->key());
    if (!publisher_buffer_cfg_.count(buffer_id))
    {
        publisher_buffer_cfg_.insert(std::make_pair(buffer_id, msg->key()));

        // check for new subbuffers
        // TODO see if there's a change to the buffer configuration with this publication
        for (const auto& sub_id_p : subscriber_buffer_cfg_)
        {
            const auto& sub_map = sub_id_p.second;
            auto sub_it = sub_map.find(buffer_id);
            if (sub_it != sub_map.end())
            {
                auto dest_id = sub_id_p.first;
                const auto& intervehicle_subscription = sub_it->second;
                _create_buffer(dest_id, buffer_id,
                               {msg->key().cfg().intervehicle().buffer(),
                                intervehicle_subscription.intervehicle().buffer()});
            }
        }
    }

    // push to all subscribed buffers
    for (auto dest_id : subbuffers_created_[buffer_id])
    {
        auto exceeded = buffer_.push({dest_id, buffer_id, goby::time::SteadyClock::now(), *msg});
        if (!exceeded.empty())
        {
            glog.is_warn() && glog << "Send buffer exceeded for " << msg->key().ShortDebugString()
                                   << std::endl;
        }
    }
}

void goby::middleware::intervehicle::ModemDriverThread::_receive(
    const goby::acomms::protobuf::ModemTransmission& rx_msg)
{
    glog.is(DEBUG1) && glog << "Received: " << rx_msg.ShortDebugString() << std::endl;
    if (rx_msg.type() == goby::acomms::protobuf::ModemTransmission::ACK)
    {
        if (rx_msg.dest() != cfg().driver().modem_id())
        {
            glog.is(WARN) && glog << "ignoring ack for modem_id = " << rx_msg.dest() << std::endl;
            return;
        }
        for (int i = 0, n = rx_msg.acked_frame_size(); i < n; ++i)
        {
            int frame_number = rx_msg.acked_frame(i);
            if (!pending_ack_.count(frame_number))
            {
                glog.is(DEBUG1) && glog << "got ack but we were not expecting one from "
                                        << rx_msg.src() << " for frame " << frame_number
                                        << std::endl;
                continue;
            }
            else
            {
                auto values_to_ack_it = pending_ack_.find(frame_number);
                glog.is(DEBUG1) && glog << "processing " << values_to_ack_it->second.size()
                                        << " acks for frame: " << frame_number << std::endl;
                for (const auto& value : values_to_ack_it->second)
                {
                    goby::glog.is_debug1() && goby::glog << "Publishing ack for "
                                                         << value.subbuffer_id << std::endl;

                    interthread_->publish<groups::modem_ack_in>(std::make_pair(value.data, rx_msg));
                    buffer_.erase(value);
                }
                pending_ack_.erase(values_to_ack_it);
                // TODO publish acks for other drivers to erase the same piece of data (if they have it)
            }
        }
    }
    else
    {
        if (rx_msg.dest() == goby::acomms::BROADCAST_ID ||
            rx_msg.dest() == cfg().driver().modem_id())
        {
            interthread_->publish<groups::modem_data_in>(rx_msg);
        }
    }
}
