// Copyright 2015-2023:
//   GobySoft, LLC (2013-)
//   Community contributors (see AUTHORS file)
// File authors:
//   Toby Schneider <toby@gobysoft.org>
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

#include <chrono>    // for opera...
#include <exception> // for excep...
#include <list>      // for opera...
#include <ostream>   // for basic...
#include <set>       // for set
#include <utility>   // for make_...

#include <boost/asio/buffer.hpp>                     // for mutab...
#include <boost/asio/completion_condition.hpp>       // for trans...
#include <boost/asio/error.hpp>                      // for host_...
#include <boost/asio/ip/basic_resolver.hpp>          // for basic...
#include <boost/asio/ip/basic_resolver_entry.hpp>    // for basic...
#include <boost/asio/ip/basic_resolver_iterator.hpp> // for opera...
#include <boost/asio/ip/tcp.hpp>                     // for tcp::...
#include <boost/asio/read.hpp>                       // for async...
#include <boost/asio/write.hpp>                      // for write
#include <boost/bimap.hpp>
#include <boost/bind/bind.hpp>                     // for bind_t
#include <boost/function.hpp>                      // for function
#include <boost/iterator/iterator_facade.hpp>      // for opera...
#include <boost/lexical_cast/bad_lexical_cast.hpp> // for bad_l...
#include <boost/multi_index/sequenced_index.hpp>   // for opera...
#include <boost/signals2/expired_slot.hpp>         // for expir...
#include <boost/signals2/mutex.hpp>                // for mutex
#include <boost/signals2/signal.hpp>               // for signal
#include <boost/smart_ptr/shared_ptr.hpp>          // for share...
#include <boost/system/error_code.hpp>             // for error...
#include <boost/system/system_error.hpp>           // for syste...
#include <boost/units/quantity.hpp>                // for opera...
#include <boost/units/systems/si/time.hpp>         // for seconds

#include "goby/acomms/acomms_constants.h"                  // for BITS_...
#include "goby/acomms/modemdriver/iridium_driver_common.h" // for OnCal...
#include "goby/acomms/modemdriver/iridium_shore_rudics.h"  // for RUDIC...
#include "goby/acomms/modemdriver/rudics_packet.h"         // for Rudic...
#include "goby/time/convert.h"                             // for Syste...
#include "goby/time/system_clock.h"                        // for Syste...
#include "goby/time/types.h"                               // for SITime
#include "goby/util/as.h"                                  // for as
#include "goby/util/asio_compat.h"                         // for io_context
#include "goby/util/binary.h"                              // for hex_e...
#include "goby/util/debug_logger/flex_ostream.h"           // for opera...
#include "goby/util/debug_logger/flex_ostreambuf.h"        // for DEBUG1
#include "goby/util/debug_logger/logger_manipulators.h"    // for opera...

#include "iridium_shore_driver.h"

using namespace goby::util::logger;
using goby::glog;

goby::acomms::IridiumShoreDriver::IridiumShoreDriver() { init_iridium_dccl(); }

goby::acomms::IridiumShoreDriver::~IridiumShoreDriver() = default;

void goby::acomms::IridiumShoreDriver::startup(const protobuf::DriverConfig& cfg)
{
    driver_cfg_ = cfg;

    glog.is(DEBUG1) && glog << group(glog_out_group())
                            << "Goby Shore Iridium RUDICS/SBD driver starting up." << std::endl;

    rudics_mac_msg_.set_src(driver_cfg_.modem_id());
    rudics_mac_msg_.set_type(goby::acomms::protobuf::ModemTransmission::DATA);
    rudics_mac_msg_.set_rate(RATE_RUDICS);

    rudics_server_.reset(
        new RUDICSServer(rudics_io_, iridium_shore_driver_cfg().rudics_server_port()));

    switch (iridium_shore_driver_cfg().sbd_type())
    {
        case goby::acomms::iridium::protobuf::ShoreConfig::SBD_DIRECTIP:
            startup_sbd_directip(cfg);
            break;

        case goby::acomms::iridium::protobuf::ShoreConfig::SBD_ROCKBLOCK:
#ifdef HAS_OPENSSL
            startup_sbd_rockblock(cfg);
#else
            glog.is_die() &&
                glog << "Rockblock functionality requires OpenSSL. Recompile with -Denable_openssl"
                     << std::endl;
#endif
            break;
    }

    rudics_server_->connect_signal.connect(
        boost::bind(&IridiumShoreDriver::rudics_connect, this, boost::placeholders::_1));

    for (int i = 0, n = iridium_shore_driver_cfg().modem_id_to_imei_size(); i < n; ++i)
        modem_id_to_imei_[iridium_shore_driver_cfg().modem_id_to_imei(i).modem_id()] =
            iridium_shore_driver_cfg().modem_id_to_imei(i).imei();

    modem_start(driver_cfg_);
}

void goby::acomms::IridiumShoreDriver::shutdown() {}

void goby::acomms::IridiumShoreDriver::handle_initiate_transmission(
    const protobuf::ModemTransmission& orig_msg)
{
    process_transmission(orig_msg);
}

void goby::acomms::IridiumShoreDriver::process_transmission(protobuf::ModemTransmission msg)
{
    signal_modify_transmission(&msg);

    if (!msg.has_frame_start())
        msg.set_frame_start(next_frame_);

    // set the frame size, if not set or if it exceeds the max configured
    if (!msg.has_max_frame_bytes() || msg.max_frame_bytes() > iridium_driver_cfg().max_frame_size())
        msg.set_max_frame_bytes(iridium_driver_cfg().max_frame_size());

    signal_data_request(&msg);

    next_frame_ += msg.frame_size();

    if (!(msg.frame_size() == 0 || msg.frame(0).empty()))
    {
        send(msg);
    }
}

void goby::acomms::IridiumShoreDriver::do_work()
{
    //   if(glog.is(DEBUG1))
    //    display_state_cfg(&glog);
    double now = time::SystemClock::now().time_since_epoch() / std::chrono::seconds(1);

    for (auto& it : remote_)
    {
        RemoteNode& remote = it.second;
        std::shared_ptr<OnCallBase> on_call_base = remote.on_call;
        ModemId id = it.first;

        // if we're on either type of call, see if we need to send the "bye" message or hangup
        if (on_call_base)
        {
            // if we're on a call, keep pushing data at the target rate
            const double send_wait =
                on_call_base->last_bytes_sent() /
                (iridium_driver_cfg().target_bit_rate() / static_cast<double>(BITS_IN_BYTE));

            if (now > (on_call_base->last_tx_time() + send_wait))
            {
                if (!on_call_base->bye_sent())
                {
                    rudics_mac_msg_.set_dest(it.first);
                    process_transmission(rudics_mac_msg_);
                }
            }

            if (!on_call_base->bye_sent() &&
                now > (on_call_base->last_tx_time() +
                       iridium_driver_cfg().handshake_hangup_seconds()))
            {
                glog.is(DEBUG1) && glog << "Sending bye" << std::endl;
                rudics_send("bye\r", id);
                on_call_base->set_bye_sent(true);
            }

            if ((on_call_base->bye_received() && on_call_base->bye_sent()) ||
                (now > (on_call_base->last_rx_tx_time() +
                        iridium_driver_cfg().hangup_seconds_after_empty())))
            {
                glog.is(DEBUG1) && glog << "Hanging up by disconnecting" << std::endl;
                typedef boost::bimap<ModemId, std::shared_ptr<RUDICSConnection>>::left_map::iterator
                    LeftIt;
                LeftIt client_it = clients_.left.find(id);
                if (client_it != clients_.left.end())
                    rudics_server_->disconnect(client_it->second);
                else
                    glog.is(WARN) && glog << "Failed to find connection from ModemId " << id
                                          << std::endl;
                remote_[id].on_call.reset();
            }
        }
    }

    rudics_io_.poll();
    receive_sbd_mo();
}

void goby::acomms::IridiumShoreDriver::receive(const protobuf::ModemTransmission& msg)
{
    glog.is(DEBUG2) && glog << group(glog_in_group()) << msg.DebugString() << std::endl;

    if (msg.type() == protobuf::ModemTransmission::DATA && msg.ack_requested() &&
        msg.dest() == driver_cfg_.modem_id())
    {
        // make any acks
        protobuf::ModemTransmission ack;
        ack.set_type(goby::acomms::protobuf::ModemTransmission::ACK);
        ack.set_src(msg.dest());
        ack.set_dest(msg.src());
        ack.set_rate(msg.rate());
        for (int i = msg.frame_start(), n = msg.frame_size() + msg.frame_start(); i < n; ++i)
            ack.add_acked_frame(i);
        send(ack);
    }

    signal_receive(msg);
}

void goby::acomms::IridiumShoreDriver::send(const protobuf::ModemTransmission& msg)
{
    glog.is(DEBUG2) && glog << group(glog_out_group()) << msg.DebugString() << std::endl;

    RemoteNode& remote = remote_[msg.dest()];

    if (msg.rate() == RATE_RUDICS || remote.on_call)
    {
        std::string bytes;
        serialize_iridium_modem_message(&bytes, msg);

        // frame message
        std::string rudics_packet;
        serialize_rudics_packet(bytes, &rudics_packet);
        rudics_send(rudics_packet, msg.dest());
        std::shared_ptr<OnCallBase> on_call_base = remote.on_call;
        on_call_base->set_last_tx_time(time::SystemClock::now().time_since_epoch() /
                                       std::chrono::seconds(1));
        on_call_base->set_last_bytes_sent(rudics_packet.size());
    }
    else if (msg.rate() == RATE_SBD)
    {
        std::string bytes;
        serialize_iridium_modem_message(&bytes, msg);

        std::string sbd_packet;
        serialize_rudics_packet(bytes, &sbd_packet);

        if (modem_id_to_imei_.count(msg.dest()))
            send_sbd_mt(sbd_packet, modem_id_to_imei_[msg.dest()]);
        else
            glog.is(WARN) && glog << "No IMEI configured for destination address " << msg.dest()
                                  << " so unabled to send SBD message." << std::endl;
    }
}

void goby::acomms::IridiumShoreDriver::rudics_send(const std::string& data,
                                                   goby::acomms::IridiumShoreDriver::ModemId id)
{
    using LeftIt = boost::bimap<ModemId, std::shared_ptr<RUDICSConnection>>::left_map::iterator;
    LeftIt client_it = clients_.left.find(id);
    if (client_it != clients_.left.end())
    {
        glog.is(DEBUG1) && glog << "RUDICS sending bytes: " << goby::util::hex_encode(data)
                                << std::endl;
        client_it->second->write_start(data);
    }
    else
    {
        glog.is(WARN) && glog << "Failed to find connection from ModemId " << id << std::endl;
    }
}

void goby::acomms::IridiumShoreDriver::rudics_connect(
    const std::shared_ptr<RUDICSConnection>& connection)
{
    connection->line_signal.connect(boost::bind(&IridiumShoreDriver::rudics_line, this,
                                                boost::placeholders::_1, boost::placeholders::_2));
    connection->disconnect_signal.connect(
        boost::bind(&IridiumShoreDriver::rudics_disconnect, this, boost::placeholders::_1));
}

void goby::acomms::IridiumShoreDriver::rudics_disconnect(
    const std::shared_ptr<RUDICSConnection>& connection)
{
    using RightIt = boost::bimap<ModemId, std::shared_ptr<RUDICSConnection>>::right_map::iterator;

    RightIt client_it = clients_.right.find(connection);
    if (client_it != clients_.right.end())
    {
        ModemId id = client_it->second;
        remote_[id].on_call.reset();
        clients_.right.erase(client_it);
        glog.is(DEBUG1) && glog << "Disconnecting client for modem id: " << id << "; "
                                << clients_.size() << " clients remaining." << std::endl;
    }
    else
    {
        glog.is(WARN) &&
            glog << "Disconnection received from connection we do not have in the clients_ map: "
                 << connection->remote_endpoint_str() << std::endl;
    }
}

void goby::acomms::IridiumShoreDriver::rudics_line(
    const std::string& data, const std::shared_ptr<RUDICSConnection>& connection)
{
    glog.is(DEBUG1) && glog << "RUDICS received bytes: " << goby::util::hex_encode(data)
                            << std::endl;

    try
    {
        std::string decoded_line;

        if (data == "goby\r" ||
            data == "\0goby\r") // sometimes Iridium adds a 0x00 to the start of transmission
        {
            glog.is(DEBUG1) && glog << "Detected start of Goby RUDICS connection from "
                                    << connection->remote_endpoint_str() << std::endl;
        }
        else if (data == "bye\r")
        {
            using RightIt =
                boost::bimap<ModemId, std::shared_ptr<RUDICSConnection>>::right_map::iterator;

            RightIt client_it = clients_.right.find(connection);
            if (client_it != clients_.right.end())
            {
                ModemId id = client_it->second;
                glog.is(DEBUG1) && glog << "Detected bye from " << connection->remote_endpoint_str()
                                        << " ID: " << id << std::endl;
                remote_[id].on_call->set_bye_received(true);
            }
            else
            {
                glog.is(WARN) &&
                    glog << "Bye detected from connection we do not have in the clients_ map: "
                         << connection->remote_endpoint_str() << std::endl;
            }
        }
        else
        {
            parse_rudics_packet(&decoded_line, data);

            protobuf::ModemTransmission modem_msg;
            parse_iridium_modem_message(decoded_line, &modem_msg);

            glog.is(DEBUG1) && glog << "Received RUDICS message from: " << modem_msg.src()
                                    << " to: " << modem_msg.dest()
                                    << " from endpoint: " << connection->remote_endpoint_str()
                                    << std::endl;
            if (!clients_.left.count(modem_msg.src()))
            {
                clients_.left.insert(std::make_pair(modem_msg.src(), connection));
                remote_[modem_msg.src()].on_call.reset(new OnCallBase);
            }

            remote_[modem_msg.src()].on_call->set_last_rx_time(
                time::SystemClock::now<time::SITime>() / boost::units::si::seconds);

            receive(modem_msg);
        }
    }
    catch (RudicsPacketException& e)
    {
        glog.is(DEBUG1) && glog << warn << "Could not decode packet: " << e.what() << std::endl;
        connection->add_packet_failure();
    }
}

void goby::acomms::IridiumShoreDriver::receive_sbd_mo()
{
    switch (iridium_shore_driver_cfg().sbd_type())
    {
        case goby::acomms::iridium::protobuf::ShoreConfig::SBD_DIRECTIP:
            receive_sbd_mo_directip();
            break;

        case goby::acomms::iridium::protobuf::ShoreConfig::SBD_ROCKBLOCK:
#ifdef HAS_OPENSSL
            receive_sbd_mo_rockblock();
#else
            glog.is_die() &&
                glog << "Rockblock functionality requires OpenSSL. Recompile with -Denable_openssl"
                     << std::endl;
#endif
            break;
    }
}

void goby::acomms::IridiumShoreDriver::receive_sbd_mo_data(const std::string& data)
{
    std::string bytes;
    protobuf::ModemTransmission modem_msg;
    try
    {
        parse_rudics_packet(&bytes, data);
        parse_iridium_modem_message(bytes, &modem_msg);

        glog.is(DEBUG1) && glog << group(glog_in_group())
                                << "Rx SBD ModemTransmission: " << modem_msg.ShortDebugString()
                                << std::endl;

        receive(modem_msg);
    }
    catch (RudicsPacketException& e)
    {
        glog.is(DEBUG1) && glog << warn << group(glog_in_group())
                                << "Could not decode SBD packet: " << e.what() << std::endl;
    }
}

void goby::acomms::IridiumShoreDriver::send_sbd_mt(const std::string& bytes,
                                                   const std::string& imei)
{
    switch (iridium_shore_driver_cfg().sbd_type())
    {
        case goby::acomms::iridium::protobuf::ShoreConfig::SBD_DIRECTIP:
            send_sbd_mt_directip(bytes, imei);
            break;

        case goby::acomms::iridium::protobuf::ShoreConfig::SBD_ROCKBLOCK:
#ifdef HAS_OPENSSL
            send_sbd_mt_rockblock(bytes, imei);
#else
            glog.is_die() &&
                glog << "Rockblock functionality requires OpenSSL. Recompile with -Denable_openssl"
                     << std::endl;
#endif
            break;
    }
}
