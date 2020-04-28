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

#include "udp_multicast_driver.h"

#include "goby/acomms/modemdriver/driver_exception.h"
#include "goby/util/binary.h"
#include "goby/util/debug_logger.h"
#include "goby/util/protobuf/io.h"

using goby::glog;
using goby::util::hex_decode;
using goby::util::hex_encode;
using namespace goby::util::logger;

goby::acomms::UDPMulticastDriver::UDPMulticastDriver() {}
goby::acomms::UDPMulticastDriver::~UDPMulticastDriver() {}

void goby::acomms::UDPMulticastDriver::startup(const protobuf::DriverConfig& cfg)
{
    driver_cfg_ = cfg;

    rate_to_bytes_.clear();

    for (const auto& rate_bytes_pair : multicast_driver_cfg().rate_to_bytes())
        rate_to_bytes_[rate_bytes_pair.rate()] = rate_bytes_pair.bytes();

    boost::asio::ip::udp::endpoint listen_endpoint(
        boost::asio::ip::address::from_string(multicast_driver_cfg().listen_address()),
        multicast_driver_cfg().multicast_port());
    socket_.open(listen_endpoint.protocol());
    socket_.set_option(boost::asio::ip::udp::socket::reuse_address(true));
    socket_.bind(listen_endpoint);

    auto multicast_address =
        boost::asio::ip::address::from_string(multicast_driver_cfg().multicast_address());

    socket_.set_option(boost::asio::ip::multicast::join_group(multicast_address));

    receiver_ =
        boost::asio::ip::udp::endpoint(multicast_address, multicast_driver_cfg().multicast_port());

    start_receive();
}

void goby::acomms::UDPMulticastDriver::shutdown()
{
    io_context_.stop();
    socket_.close();
}

void goby::acomms::UDPMulticastDriver::handle_initiate_transmission(
    const protobuf::ModemTransmission& orig_msg)
{
    // buffer the message
    protobuf::ModemTransmission msg = orig_msg;
    signal_modify_transmission(&msg);

    if (!msg.has_frame_start())
        msg.set_frame_start(next_frame_);

    if (!msg.has_max_frame_bytes())
    {
        if (rate_to_bytes_.count(msg.rate()))
            msg.set_max_frame_bytes(rate_to_bytes_[msg.rate()]);
        else
            msg.set_max_frame_bytes(multicast_driver_cfg().max_frame_size());
    }

    signal_data_request(&msg);

    glog.is(DEBUG1) && glog << group(glog_out_group())
                            << "After modification, initiating transmission with " << msg
                            << std::endl;

    next_frame_ += msg.frame_size();

    if (!(msg.frame_size() == 0 || msg.frame(0).empty()))
        start_send(msg);
}

void goby::acomms::UDPMulticastDriver::do_work() { io_context_.poll(); }

void goby::acomms::UDPMulticastDriver::receive_message(const protobuf::ModemTransmission& msg)
{
    if (msg.type() == protobuf::ModemTransmission::DATA && msg.ack_requested() &&
        msg.dest() != BROADCAST_ID)
    {
        // make any acks
        protobuf::ModemTransmission ack;
        ack.set_type(goby::acomms::protobuf::ModemTransmission::ACK);
        ack.set_src(driver_cfg_.modem_id());
        ack.set_dest(msg.src());
        for (int i = msg.frame_start(), n = msg.frame_size() + msg.frame_start(); i < n; ++i)
            ack.add_acked_frame(i);
        start_send(ack);
    }

    signal_receive(msg);
}

void goby::acomms::UDPMulticastDriver::start_send(const google::protobuf::Message& msg)
{
    // send the message
    std::string bytes;
    msg.SerializeToString(&bytes);

    glog.is(DEBUG1) && glog << group(glog_out_group())
                            << "Sending hex: " << goby::util::hex_encode(bytes) << std::endl;

    protobuf::ModemRaw raw_msg;
    raw_msg.set_raw(bytes);
    signal_raw_outgoing(raw_msg);

    socket_.async_send_to(
        boost::asio::buffer(bytes), receiver_,
        [this](boost::system::error_code ec, std::size_t length) { send_complete(ec, length); });
}

void goby::acomms::UDPMulticastDriver::send_complete(const boost::system::error_code& error,
                                                     std::size_t bytes_transferred)
{
    if (error)
    {
        glog.is(DEBUG1) && glog << group(glog_out_group()) << warn
                                << "Send error: " << error.message() << std::endl;
        return;
    }

    glog.is(DEBUG1) && glog << group(glog_out_group()) << "Sent " << bytes_transferred << " bytes."
                            << std::endl;
}

void goby::acomms::UDPMulticastDriver::start_receive()
{
    socket_.async_receive_from(
        boost::asio::buffer(receive_buffer_), sender_,
        [this](boost::system::error_code ec, std::size_t length) { receive_complete(ec, length); });
}

void goby::acomms::UDPMulticastDriver::receive_complete(const boost::system::error_code& error,
                                                        std::size_t bytes_transferred)
{
    if (error)
    {
        glog.is(DEBUG1) && glog << group(glog_in_group()) << warn
                                << "Receive error: " << error.message() << std::endl;
        start_receive();
        return;
    }

    protobuf::ModemRaw raw_msg;
    raw_msg.set_raw(std::string(&receive_buffer_[0], bytes_transferred));
    signal_raw_incoming(raw_msg);

    protobuf::ModemTransmission msg;
    msg.ParseFromArray(&receive_buffer_[0], bytes_transferred);

    // reject messages to ourselves
    if (msg.src() != driver_cfg_.modem_id())
    {
        glog.is(DEBUG1) && glog << group(glog_in_group()) << "Received " << bytes_transferred
                                << " bytes from " << sender_.address().to_string() << ":"
                                << sender_.port() << std::endl;

        receive_message(msg);
    }

    start_receive();
}
