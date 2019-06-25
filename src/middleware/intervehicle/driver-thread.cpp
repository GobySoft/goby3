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
#include "goby/acomms/modemdriver/iridium_driver.h"
#include "goby/acomms/modemdriver/iridium_shore_driver.h"
#include "goby/acomms/modemdriver/udp_driver.h"

#include "driver-thread.h"

using goby::glog;
using namespace goby::util::logger;

goby::middleware::intervehicle::ModemDriverThread::ModemDriverThread(
    const protobuf::InterVehiclePortalConfig::LinkConfig& config)
    : goby::middleware::Thread<protobuf::InterVehiclePortalConfig::LinkConfig,
                               InterThreadTransporter>(config, 10 * boost::units::si::hertz)
{
    interthread_.reset(new InterThreadTransporter);
    this->set_transporter(interthread_.get());

    interthread_->subscribe<groups::modem_data_out, protobuf::SerializerTransporterMessage>(
        [this](std::shared_ptr<const protobuf::SerializerTransporterMessage> msg) {
            _buffer_message(msg);
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

    driver_->signal_receive.connect([&](const goby::acomms::protobuf::ModemTransmission& rx_msg) {
        interthread_->publish<groups::modem_data_in>(rx_msg);
        glog.is(DEBUG1) && glog << "Received: " << rx_msg.ShortDebugString() << std::endl;
    });
    driver_->signal_data_request.connect(
        [&](goby::acomms::protobuf::ModemTransmission* msg) { this->_data_request(msg); });

    goby::acomms::bind(mac_, *driver_);

    //q_manager_.set_cfg(cfg().queue_cfg());
    mac_.startup(cfg().mac());
    driver_->startup(cfg().driver());

    goby::glog.is_debug1() && goby::glog << "Driver ready" << std::endl;
    interthread_->publish<groups::modem_driver_ready, bool>(true);
}

void goby::middleware::intervehicle::ModemDriverThread::loop()
{
    driver_->do_work();
    mac_.do_work();
}

void goby::middleware::intervehicle::ModemDriverThread::_data_request(
    goby::acomms::protobuf::ModemTransmission* msg)
{
    for (auto frame_number = msg->frame_start(),
              total_frames = msg->max_num_frames() + msg->frame_start();
         frame_number < total_frames; ++frame_number)
    {
        std::string* frame = msg->add_frame();

        while (frame->size() < msg->max_frame_bytes())
        {
            try
            {
                auto buffer_value = buffer_.top(msg->max_frame_bytes() - frame->size());
                *frame += std::get<2>(buffer_value);

                // to do: replace with ack
                buffer_.erase(buffer_value);
            }
            catch (goby::acomms::DynamicBufferNoDataException&)
            {
                break;
            }
        }
    }
}

void goby::middleware::intervehicle::ModemDriverThread::_buffer_message(
    std::shared_ptr<const protobuf::SerializerTransporterMessage> msg)
{
    auto buffer_id = create_buffer_id(msg->key());
    if (!publisher_buffer_cfg_.count(buffer_id))
    {
        buffer_.create(buffer_id, msg->key().cfg().intervehicle().buffer());
        publisher_buffer_cfg_.insert(std::make_pair(buffer_id, msg));
    }

    auto exceeded = buffer_.push(buffer_id, msg->data());
    if (!exceeded.empty())
    {
        glog.is_warn() && glog << "Send buffer exceeded for " << msg->key().ShortDebugString()
                               << std::endl;
    }
}
