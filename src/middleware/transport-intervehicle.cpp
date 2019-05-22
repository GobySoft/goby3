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

#include "transport-intervehicle.h"

using goby::glog;
using namespace goby::common::logger;

goby::ModemDriverThread::ModemDriverThread(const protobuf::InterVehiclePortalConfig& cfg,
                                           std::atomic<bool>& alive,
                                           std::shared_ptr<std::condition_variable_any> poller_cv)
    : cfg_(cfg), alive_(alive), poller_cv_(poller_cv)
{
    switch (cfg_.driver_type())
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
                                     goby::acomms::protobuf::DriverType_Name(cfg_.driver_type())));
            break;
    }

    driver_->signal_receive.connect(
        [&](const goby::acomms::protobuf::ModemTransmission& rx_msg) { this->_receive(rx_msg); });
    driver_->signal_data_request.connect(
        [&](goby::acomms::protobuf::ModemTransmission* msg) { this->_data_request(msg); });

    goby::acomms::bind(mac_, *driver_);

    //q_manager_.set_cfg(cfg_.queue_cfg());
    mac_.startup(cfg_.mac_cfg());
    driver_->startup(cfg_.driver_cfg());

    for (const auto& lib_path : cfg_.dccl_load_library())
        DCCLSerializerParserHelperBase::load_library(lib_path);
}

void goby::ModemDriverThread::run()
{
    while (alive_)
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            driver_->do_work();
            mac_.do_work();
            //q_manager_.do_work();
        }
        // run at ~10Hz
        usleep(100000);
    }
}

void goby::ModemDriverThread::publish(const std::string& bytes)
{
    std::lock_guard<std::mutex> lock(mutex_);
    sending_.push_back(bytes);
}

bool goby::ModemDriverThread::retrieve_message(goby::acomms::protobuf::ModemTransmission* msg)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (received_.empty())
    {
        return false;
    }
    else
    {
        *msg = received_.front();
        received_.pop_front();
        return true;
    }
}

void goby::ModemDriverThread::_receive(const goby::acomms::protobuf::ModemTransmission& rx_msg)
{
    received_.push_back(rx_msg);
    poller_cv_->notify_all();
    glog.is(DEBUG1) && glog << "Received: " << rx_msg.ShortDebugString() << std::endl;
}

void goby::ModemDriverThread::_data_request(goby::acomms::protobuf::ModemTransmission* msg)
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
