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

#include "store_server_driver.h"

#include "goby/acomms/modemdriver/driver_exception.h"
#include "goby/acomms/modemdriver/mm_driver.h"
#include "goby/time/system_clock.h"
#include "goby/time/types.h"
#include "goby/util/binary.h"
#include "goby/util/debug_logger.h"

using goby::glog;
using goby::util::hex_decode;
using goby::util::hex_encode;
using namespace goby::util::logger;

goby::acomms::StoreServerDriver::StoreServerDriver()
    : last_send_time_(goby::time::SystemClock::now<goby::time::MicroTime>().value()),
      request_socket_id_(0),
      query_interval_seconds_(1),
      reset_interval_seconds_(120),
      waiting_for_reply_(false),
      next_frame_(0)
{
}

void goby::acomms::StoreServerDriver::startup(const protobuf::DriverConfig& cfg)
{
    driver_cfg_ = cfg;
    store_server_driver_cfg_ = driver_cfg_.GetExtension(store_server::protobuf::config);

    if (!driver_cfg_.has_tcp_port())
        driver_cfg_.set_tcp_port(default_port);

    request_.set_modem_id(driver_cfg_.modem_id());

    query_interval_seconds_ = store_server_driver_cfg_.query_interval_seconds();
    reset_interval_seconds_ = store_server_driver_cfg_.reset_interval_seconds();

    modem_start(driver_cfg_);
}

void goby::acomms::StoreServerDriver::shutdown() {}

void goby::acomms::StoreServerDriver::handle_initiate_transmission(
    const protobuf::ModemTransmission& orig_msg)
{
    switch (orig_msg.type())
    {
        case protobuf::ModemTransmission::DATA:
        {
            // buffer the message
            protobuf::ModemTransmission msg = orig_msg;
            signal_modify_transmission(&msg);

            if (driver_cfg_.modem_id() == msg.src())
            {
                if (!msg.has_frame_start())
                    msg.set_frame_start(next_frame_);

                // this is our transmission
                if (msg.rate() < store_server_driver_cfg_.rate_to_bytes_size())
                    msg.set_max_frame_bytes(store_server_driver_cfg_.rate_to_bytes(msg.rate()));
                else
                    msg.set_max_frame_bytes(store_server_driver_cfg_.max_frame_size());

                if (msg.rate() < store_server_driver_cfg_.rate_to_frames_size())
                    msg.set_max_num_frames(store_server_driver_cfg_.rate_to_frames(msg.rate()));

                // no data given to us, let's ask for some
                if (msg.frame_size() < (int)msg.max_num_frames())
                    ModemDriverBase::signal_data_request(&msg);

                next_frame_ += msg.frame_size();

                // don't send an empty message
                if (msg.frame_size() && msg.frame(0).size())
                {
                    *request_.add_outbox() = msg;
                }
            }
            else
            {
                // send thirdparty "poll"

                store_server::protobuf::Transmission& store_server_transmission =
                    *msg.MutableExtension(store_server::protobuf::transmission);

                store_server_transmission.set_poll_src(msg.src());
                store_server_transmission.set_poll_dest(msg.dest());

                msg.set_dest(msg.src());
                msg.set_src(driver_cfg_.modem_id());

                msg.set_type(protobuf::ModemTransmission::DRIVER_SPECIFIC);
                store_server_transmission.set_type(
                    store_server::protobuf::Transmission::STORE_SERVER_DRIVER_POLL);

                *request_.add_outbox() = msg;
            }
        }
        break;
        default:
            glog.is(DEBUG1) && glog << group(glog_out_group()) << warn
                                    << "Not initiating transmission because we were given an "
                                       "invalid transmission type for the base Driver:"
                                    << orig_msg.DebugString() << std::endl;
            break;
    }
}

void goby::acomms::StoreServerDriver::do_work()
{
    std::string in;
    while (modem_read(&in))
    {
        protobuf::StoreServerResponse response;
        try
        {
            parse_store_server_message(in, &response);
            handle_response(response);
        }
        catch (const std::exception& e)
        {
            glog.is(WARN) && glog << "Failed to parse response from goby_store_server" << std::endl;
        }
    }

    // call in with our outbox
    if (!waiting_for_reply_ && request_.IsInitialized() &&
        goby::time::SystemClock::now<goby::time::MicroTime>().value() >
            last_send_time_ + 1000000 * static_cast<std::uint64_t>(query_interval_seconds_))
    {
        static int request_id = 0;
        request_.set_request_id(request_id++);
        glog.is(DEBUG1) && glog << group(glog_out_group()) << "Sending to server." << std::endl;
        glog.is(DEBUG2) && glog << group(glog_out_group()) << "Outbox: " << request_.DebugString()
                                << std::flush;

        std::string request_bytes;
        serialize_store_server_message(request_, &request_bytes);
        modem_write(request_bytes);
        last_send_time_ = goby::time::SystemClock::now<goby::time::MicroTime>().value();
        request_.clear_outbox();
        waiting_for_reply_ = true;
    }
    else if (waiting_for_reply_ && goby::time::SystemClock::now<goby::time::MicroTime>().value() >
                                       last_send_time_ + 1e6 * reset_interval_seconds_)
    {
        glog.is(DEBUG1) && glog << group(glog_out_group()) << warn << "No response in "
                                << reset_interval_seconds_ << " seconds, resetting socket."
                                << std::endl;

        modem_close();
        modem_start(driver_cfg_);

        waiting_for_reply_ = false;
    }
}

void goby::acomms::StoreServerDriver::handle_response(const protobuf::StoreServerResponse& response)
{
    glog.is(DEBUG1) &&
        glog << group(glog_in_group()) << "Received response in "
             << (goby::time::SystemClock::now<goby::time::MicroTime>().value() - last_send_time_) /
                    1.0e6
             << " seconds." << std::endl;

    glog.is(DEBUG2) && glog << group(glog_in_group()) << "Inbox: " << response.DebugString()
                            << std::flush;

    for (int i = 0, n = response.inbox_size(); i < n; ++i)
    {
        const protobuf::ModemTransmission& msg = response.inbox(i);

        const store_server::protobuf::Transmission& store_server_transmission =
            msg.GetExtension(store_server::protobuf::transmission);
        // poll for us
        if (msg.type() == protobuf::ModemTransmission::DRIVER_SPECIFIC &&
            store_server_transmission.type() ==
                store_server::protobuf::Transmission::STORE_SERVER_DRIVER_POLL &&
            store_server_transmission.poll_src() == driver_cfg_.modem_id())
        {
            protobuf::ModemTransmission data_msg = msg;
            data_msg.ClearExtension(store_server::protobuf::transmission);

            data_msg.set_type(protobuf::ModemTransmission::DATA);
            data_msg.set_src(store_server_transmission.poll_src());
            data_msg.set_dest(store_server_transmission.poll_dest());

            handle_initiate_transmission(data_msg);
        }
        else
        {
            // ack any packets
            if (msg.dest() == driver_cfg_.modem_id() &&
                msg.type() == protobuf::ModemTransmission::DATA && msg.ack_requested())
            {
                protobuf::ModemTransmission& ack = *request_.add_outbox();
                ack.set_type(protobuf::ModemTransmission::ACK);
                ack.set_src(msg.dest());
                ack.set_dest(msg.src());
                for (int i = msg.frame_start(), n = msg.frame_size() + msg.frame_start(); i < n;
                     ++i)
                    ack.add_acked_frame(i);
            }

            signal_receive(msg);
        }
    }

    waiting_for_reply_ = false;
}
