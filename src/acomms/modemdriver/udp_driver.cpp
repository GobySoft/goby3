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

#include "udp_driver.h"

#include "goby/acomms/modemdriver/driver_exception.h"
#include "goby/acomms/modemdriver/mm_driver.h"
#include "goby/util/binary.h"
#include "goby/util/debug_logger.h"
#include "goby/util/protobuf/io.h"

using goby::glog;
using goby::util::hex_decode;
using goby::util::hex_encode;
using namespace goby::util::logger;

const size_t UDP_MAX_PACKET_SIZE = 65507; // (16 bit length = 65535 - 8 byte UDP header -20 byte IP)

goby::acomms::UDPDriver::UDPDriver(boost::asio::io_service* io_service)
    : io_service_(io_service),
      socket_(*io_service),
      receive_buffer_(UDP_MAX_PACKET_SIZE),
      next_frame_(0)
{
}

goby::acomms::UDPDriver::~UDPDriver() {}

void goby::acomms::UDPDriver::startup(const protobuf::DriverConfig& cfg)
{
    driver_cfg_ = cfg;

    const UDPDriverConfig::EndPoint& local = driver_cfg_.GetExtension(UDPDriverConfig::local);
    socket_.open(boost::asio::ip::udp::v4());
    socket_.bind(boost::asio::ip::udp::endpoint(boost::asio::ip::udp::v4(), local.port()));

    const UDPDriverConfig::EndPoint& remote = driver_cfg_.GetExtension(UDPDriverConfig::remote);
    boost::asio::ip::udp::resolver resolver(*io_service_);
    boost::asio::ip::udp::resolver::query query(boost::asio::ip::udp::v4(), remote.ip(),
                                                goby::util::as<std::string>(remote.port()));
    boost::asio::ip::udp::resolver::iterator endpoint_iterator = resolver.resolve(query);
    receiver_ = *endpoint_iterator;

    glog.is(DEBUG1) && glog << group(glog_out_group())
                            << "Receiver endpoint is: " << receiver_.address().to_string() << ":"
                            << receiver_.port() << std::endl;

    start_receive();
    io_service_->reset();
}

void goby::acomms::UDPDriver::shutdown()
{
    io_service_->stop();
    socket_.close();
}

void goby::acomms::UDPDriver::handle_initiate_transmission(
    const protobuf::ModemTransmission& orig_msg)
{
    // buffer the message
    protobuf::ModemTransmission msg = orig_msg;
    signal_modify_transmission(&msg);

    if (!msg.has_frame_start())
        msg.set_frame_start(next_frame_);

    msg.set_max_frame_bytes(driver_cfg_.GetExtension(UDPDriverConfig::max_frame_size));
    signal_data_request(&msg);

    glog.is(DEBUG1) && glog << group(glog_out_group())
                            << "After modification, initiating transmission with " << msg
                            << std::endl;

    next_frame_ += msg.frame_size();

    if (!(msg.frame_size() == 0 || msg.frame(0).empty()))
        start_send(msg);
}

void goby::acomms::UDPDriver::do_work() { io_service_->poll(); }

void goby::acomms::UDPDriver::receive_message(const protobuf::ModemTransmission& msg)
{
    if (msg.type() == protobuf::ModemTransmission::DATA && msg.ack_requested() &&
        msg.dest() != BROADCAST_ID)
    {
        // make any acks
        protobuf::ModemTransmission ack;
        ack.set_type(goby::acomms::protobuf::ModemTransmission::ACK);
        ack.set_src(msg.dest());
        ack.set_dest(msg.src());
        for (int i = msg.frame_start(), n = msg.frame_size() + msg.frame_start(); i < n; ++i)
            ack.add_acked_frame(i);
        start_send(ack);
    }

    signal_receive(msg);
}

void goby::acomms::UDPDriver::start_send(const google::protobuf::Message& msg)
{
    // send the message
    std::string bytes;
    msg.SerializeToString(&bytes);

    glog.is(DEBUG1) && glog << group(glog_out_group())
                            << "Sending hex: " << goby::util::hex_encode(bytes) << std::endl;

    protobuf::ModemRaw raw_msg;
    raw_msg.set_raw(bytes);
    signal_raw_outgoing(raw_msg);

    socket_.async_send_to(boost::asio::buffer(bytes), receiver_,
                          boost::bind(&UDPDriver::send_complete, this, _1, _2));
}

void goby::acomms::UDPDriver::send_complete(const boost::system::error_code& error,
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

void goby::acomms::UDPDriver::start_receive()
{
    socket_.async_receive_from(boost::asio::buffer(receive_buffer_), sender_,
                               boost::bind(&UDPDriver::receive_complete, this, _1, _2));
}

void goby::acomms::UDPDriver::receive_complete(const boost::system::error_code& error,
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

    glog.is(DEBUG1) && glog << group(glog_in_group()) << "Received " << bytes_transferred
                            << " bytes from " << sender_.address().to_string() << ":"
                            << sender_.port() << std::endl;

    protobuf::ModemTransmission msg;
    msg.ParseFromArray(&receive_buffer_[0], bytes_transferred);
    receive_message(msg);

    start_receive();
}
