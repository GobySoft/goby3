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
    const protobuf::InterVehiclePortalConfig& config)
    : goby::middleware::Thread<protobuf::InterVehiclePortalConfig, InterThreadTransporter>(
          config, 10 * boost::units::si::hertz)
{
    interthread_.reset(new InterThreadTransporter);
    this->set_transporter(interthread_.get());

    interthread_->subscribe<groups::modem_data_out, std::string>(
        [this](const std::string& bytes) { sending_.push_back(bytes); });

    switch (cfg().driver_type())
    {
        case goby::acomms::protobuf::DRIVER_WHOI_MICROMODEM:
            driver_.reset(new goby::acomms::MMDriver);
            break;

        case goby::acomms::protobuf::DRIVER_IRIDIUM:
            driver_.reset(new goby::acomms::IridiumDriver);
            break;

        case goby::acomms::protobuf::DRIVER_UDP:
            asio_service_.push_back(
                std::unique_ptr<boost::asio::io_service>(new boost::asio::io_service));
            driver_.reset(new goby::acomms::UDPDriver(asio_service_.back().get()));
            break;

        case goby::acomms::protobuf::DRIVER_IRIDIUM_SHORE:
            driver_.reset(new goby::acomms::IridiumShoreDriver);
            break;

        case goby::acomms::protobuf::DRIVER_NONE: break;

        default:
            throw(std::runtime_error("Unsupported driver type: " +
                                     goby::acomms::protobuf::DriverType_Name(cfg().driver_type())));
            break;
    }

    driver_->signal_receive.connect(
        [&](const goby::acomms::protobuf::ModemTransmission& rx_msg) { this->_receive(rx_msg); });
    driver_->signal_data_request.connect(
        [&](goby::acomms::protobuf::ModemTransmission* msg) { this->_data_request(msg); });

    goby::acomms::bind(mac_, *driver_);

    //q_manager_.set_cfg(cfg().queue_cfg());
    mac_.startup(cfg().mac_cfg());
    driver_->startup(cfg().driver_cfg());

    for (const auto& lib_path : cfg().dccl_load_library())
        DCCLSerializerParserHelperBase::load_library(lib_path);
}

void goby::middleware::intervehicle::ModemDriverThread::loop()
{
    driver_->do_work();
    mac_.do_work();
}

void goby::middleware::intervehicle::ModemDriverThread::_receive(
    const goby::acomms::protobuf::ModemTransmission& rx_msg)
{
    interthread_->publish<groups::modem_data_in>(rx_msg);
    glog.is(DEBUG1) && glog << "Received: " << rx_msg.ShortDebugString() << std::endl;
}

void goby::middleware::intervehicle::ModemDriverThread::_data_request(
    goby::acomms::protobuf::ModemTransmission* msg)
{
    for (auto frame_number = msg->frame_start(),
              total_frames = msg->max_num_frames() + msg->frame_start();
         frame_number < total_frames; ++frame_number)
    {
        if (sending_.empty())
            break;

        std::string* frame = msg->add_frame();
        while (!sending_.empty() &&
               (frame->size() + sending_.front().size() <= msg->max_frame_bytes()))
        {
            *frame += sending_.front();
            sending_.pop_front();
        }
    }
}
