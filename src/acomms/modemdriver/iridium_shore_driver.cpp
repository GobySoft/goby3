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
#include "goby/acomms/modemdriver/iridium_shore_sbd.h"     // for SBDMO...
#include "goby/acomms/modemdriver/rudics_packet.h"         // for Rudic...
#include "goby/acomms/protobuf/iridium_sbd_directip.pb.h"  // for Direc...
#include "goby/time/convert.h"                             // for Syste...
#include "goby/time/system_clock.h"                        // for Syste...
#include "goby/time/types.h"                               // for SITime
#include "goby/util/as.h"                                  // for as
#include "goby/util/asio_compat.h"                         // for io_context
#include "goby/util/binary.h"                              // for hex_e...
#include "goby/util/debug_logger/flex_ostream.h"           // for opera...
#include "goby/util/debug_logger/flex_ostreambuf.h"        // for DEBUG1
#include "goby/util/debug_logger/logger_manipulators.h"    // for opera...

#include "goby/util/thirdparty/jwt-cpp/traits/nlohmann-json/defaults.h"

#include "goby/util/thirdparty/jwt-cpp/jwt.h"

#include "iridium_shore_driver.h"

using namespace goby::util::logger;
using goby::glog;
using goby::acomms::iridium::protobuf::DirectIPMTHeader;
using goby::acomms::iridium::protobuf::DirectIPMTPayload;

goby::acomms::IridiumShoreDriver::IridiumShoreDriver() { init_iridium_dccl(); }

goby::acomms::IridiumShoreDriver::~IridiumShoreDriver() = default;

// from https://docs.rock7.com/reference/push-api
const std::string rockblock_rsa_pubkey = R"(-----BEGIN PUBLIC KEY-----
MIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEAlaWAVJfNWC4XfnRx96p9cztBcdQV6l8aKmzAlZdpEcQR6MSPzlgvihaUHNJgKm8t5ShR3jcDXIOI7er30cIN4/9aVFMe0LWZClUGgCSLc3rrMD4FzgOJ4ibD8scVyER/sirRzf5/dswJedEiMte1ElMQy2M6IWBACry9u12kIqG0HrhaQOzc6Tr8pHUWTKft3xwGpxCkV+K1N+9HCKFccbwb8okRP6FFAMm5sBbw4yAu39IVvcSL43Tucaa79FzOmfGs5mMvQfvO1ua7cOLKfAwkhxEjirC0/RYX7Wio5yL6jmykAHJqFG2HT0uyjjrQWMtoGgwv9cIcI7xbsDX6owIDAQAB
-----END PUBLIC KEY-----)";

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
            directip_mo_sbd_server_.reset(
                new directip::SBDServer(sbd_io_, iridium_shore_driver_cfg().mo_sbd_server_port()));
            break;

        case goby::acomms::iridium::protobuf::ShoreConfig::SBD_ROCKBLOCK:
            if (!iridium_shore_driver_cfg().has_rockblock())
                glog.is(DIE) &&
                    glog << group(glog_out_group())
                         << "Must specify rockblock {} configuration when using SBD_ROCKBLOCK"
                         << std::endl;

            // use the build-in modem connection for receiving MO messages as HTTP is line-based
            driver_cfg_.set_connection_type(
                goby::acomms::protobuf::DriverConfig::CONNECTION_TCP_AS_SERVER);

            if (iridium_shore_driver_cfg().has_mo_sbd_server_port() || !driver_cfg_.has_tcp_port())
                driver_cfg_.set_tcp_port(iridium_shore_driver_cfg().mo_sbd_server_port());

            // new line for HTTP; JSON message has no newline so use }
            driver_cfg_.set_line_delimiter("\n|}");
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
            receive_sbd_mo_rockblock();
            break;
    }
}

void goby::acomms::IridiumShoreDriver::receive_sbd_mo_directip()
{
    try
    {
        sbd_io_.poll();
    }
    catch (std::exception& e)
    {
        glog.is(DEBUG1) && glog << warn << group(glog_in_group())
                                << "Could not handle SBD receive: " << e.what() << std::endl;
    }

    auto it = directip_mo_sbd_server_->connections().begin(),
         end = directip_mo_sbd_server_->connections().end();
    while (it != end)
    {
        const int timeout = 5;
        if ((*it)->message().data_ready())
        {
            glog.is(DEBUG1) && glog << group(glog_in_group()) << "Rx SBD PreHeader: "
                                    << (*it)->message().pre_header().DebugString() << std::endl;
            glog.is(DEBUG1) && glog << group(glog_in_group())
                                    << "Rx SBD Header: " << (*it)->message().header().DebugString()
                                    << std::endl;
            glog.is(DEBUG1) && glog << group(glog_in_group())
                                    << "Rx SBD Payload: " << (*it)->message().body().DebugString()
                                    << std::endl;

            receive_sbd_mo_data((*it)->message().body().payload());
            directip_mo_sbd_server_->connections().erase(it++);
        }
        else if ((*it)->connect_time() > 0 &&
                 (time::SystemClock::now().time_since_epoch() / std::chrono::seconds(1) >
                  ((*it)->connect_time() + timeout)))
        {
            glog.is(DEBUG1) && glog << group(glog_in_group())
                                    << "Removing SBD connection that has timed out:"
                                    << (*it)->remote_endpoint_str() << std::endl;
            directip_mo_sbd_server_->connections().erase(it++);
        }
        else
        {
            ++it;
        }
    }
}

// e.g.,

// POST / HTTP/1.1
// User-Agent: Rock7PushApi
// Content-Type: application/json; charset=utf-8
// Content-Length: 1125
// Host: gobysoft.org:8080
// Connection: Keep-Alive
// Accept-Encoding: gzip

// {"momsn":66,"data":"546865726520617265203130207479706573206f662070656f706c652077686f20756e6465727374616e642062696e617279","serial":14331,"iridium_latitude":75.0001,"iridium_cep":3.0,"JWT":"eyJhbGciOiJSUzI1NiJ9.eyJpc3MiOiJSb2NrIDciLCJpYXQiOjE2OTM4ODg0NzUsImRhdGEiOiI1NDY4NjU3MjY1MjA2MTcyNjUyMDMxMzAyMDc0Nzk3MDY1NzMyMDZmNjYyMDcwNjU2ZjcwNmM2NTIwNzc2ODZmMjA3NTZlNjQ2NTcyNzM3NDYxNmU2NDIwNjI2OTZlNjE3Mjc5IiwiZGV2aWNlX3R5cGUiOiJST0NLQkxPQ0siLCJpbWVpIjoiMzAwNDM0MDYyMDk5NDMwIiwiaXJpZGl1bV9jZXAiOiIzLjAiLCJpcmlkaXVtX2xhdGl0dWRlIjoiNzUuMDAwMSIsImlyaWRpdW1fbG9uZ2l0dWRlIjoiMTU0LjEyMDkiLCJtb21zbiI6IjY2Iiwic2VyaWFsIjoiMTQzMzEiLCJ0cmFuc21pdF90aW1lIjoiMjMtMDktMDUgMDQ6MzM6MjQifQ.XfBpmA_XmkRoK1PPBjYKm-cQzEXIV-OOjt5ODgF6TX5LMhoDlDkAA7JrFiRnCY8zYMzseLn368oBwNR01yIe8bYVIVM5MM0ykkovRve-ubmwdHYvH0Hgiph-a69yeMwzvGOyfW8WFxTJwg8I_UCCWyFwG_FIHk-iZsiAZ42h7AYDk8zpMStH3-jD5I8ymGQXRe9b-QfLlAOFfXMNBKuFRBsMw1Jgbgv5X5ScpkYrVMozOkjMFaLPSclYeE1jodlv_QUVXR-4g_Xczmdff56HnOfBTVuUwdFkr661r5EoXgX3GB-natzZ5XZ-dy8j84GQYOZi5wBmtFalKdJdmHS1rg","imei":"300434062099430","device_type":"ROCKBLOCK","transmit_time":"23-09-05 04:33:24","iridium_longitude":154.1209}
void goby::acomms::IridiumShoreDriver::receive_sbd_mo_rockblock()
{
    std::string line;
    const std::string start = "POST / HTTP/1.1";
    while (modem_read(&line))
    {
        if (boost::trim_copy(line) == start)
        {
            if (rb_msg_ && rb_msg_->state != RockblockHTTPMessage::MessageState::COMPLETE)
            {
                glog.is_warn() &&
                    glog << group(glog_in_group())
                         << "Received start of new HTTP message without completing last message"
                         << std::endl;
            }

            rb_msg_.reset(new RockblockHTTPMessage);
        }
        else if (rb_msg_)
        {
            if (rb_msg_->state == RockblockHTTPMessage::MessageState::COMPLETE)
            {
                glog.is_warn() && glog << group(glog_in_group())
                                       << "Received data after complete message, ignoring."
                                       << std::endl;
                continue;
            }
            else if (rb_msg_->state == RockblockHTTPMessage::MessageState::HEADER)
            {
                if (line == "\r\n")
                {
                    rb_msg_->state = RockblockHTTPMessage::MessageState::BODY;
                    for (const auto& h_p : rb_msg_->header)
                        glog.is_debug2() && glog << group(glog_in_group()) << "Header ["
                                                 << h_p.first << ":" << h_p.second << "]"
                                                 << std::endl;
                }
                else
                {
                    auto colon_pos = line.find(":");
                    if (colon_pos == std::string::npos)
                    {
                        glog.is_warn() && glog << group(glog_in_group())
                                               << "Received header field without colon, ignoring"
                                               << std::endl;
                        continue;
                    }
                    std::string key = line.substr(0, colon_pos);
                    std::string value = line.substr(colon_pos);

                    boost::trim_if(key, boost::is_any_of(": \r\n"));
                    boost::trim_if(value, boost::is_any_of(": \r\n"));
                    rb_msg_->header.insert(std::make_pair(key, value));
                }
            }
            else if (rb_msg_->state == RockblockHTTPMessage::MessageState::BODY)
            {
                rb_msg_->body += line;
                if (rb_msg_->header.count("Content-Length"))
                {
                    auto length = goby::util::as<int>(rb_msg_->header.at("Content-Length"));
                    if (rb_msg_->body.size() == length)
                    {
                        rb_msg_->state = RockblockHTTPMessage::MessageState::COMPLETE;

                        try
                        {
                            auto json_data = nlohmann::json::parse(rb_msg_->body);
                            glog.is_debug1() && glog << "Received valid JSON message: "
                                                     << json_data.dump(2) << std::endl;

                            auto verify = jwt::verify()
                                              .allow_algorithm(jwt::algorithm::rs256(
                                                  rockblock_rsa_pubkey, "", "", ""))
                                              .with_issuer("Rock 7");
                            auto decoded = jwt::decode(json_data["JWT"].get<std::string>());
                            try
                            {
                                verify.verify(decoded);
                                receive_sbd_mo_data(goby::util::hex_decode(json_data["data"]));
                            }
                            catch (const std::exception& e)
                            {
                                glog.is_warn() && glog << "Discarding message: could not verify "
                                                          "Rockblock JWT against public key: "
                                                       << e.what() << std::endl;
                            }

                            modem_write("HTTP/1.1 200 OK\r\n");
                            modem_write("Content-Length: 0\r\n");
                            modem_write("Connection: close\r\n\r\n");
                        }
                        catch (std::exception& e)
                        {
                            glog.is_warn() && glog << group(glog_in_group())
                                                   << "Failed to parse JSON: " << e.what()
                                                   << ", data: " << rb_msg_->body << std::endl;
                        }
                    }
                }
                else
                {
                    glog.is_warn() && glog << group(glog_in_group())
                                           << "No Content-Length in header" << std::endl;
                }
            }
        }
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
    try
    {
        using boost::asio::ip::tcp;

        boost::asio::io_service io_service;

        tcp::resolver resolver(io_service);
        tcp::resolver::query query(
            iridium_shore_driver_cfg().mt_sbd_server_address(),
            goby::util::as<std::string>(iridium_shore_driver_cfg().mt_sbd_server_port()),
            boost::asio::ip::resolver_query_base::numeric_service);
        tcp::resolver::iterator endpoint_iterator = resolver.resolve(query);
        tcp::resolver::iterator end;

        tcp::socket socket(io_service);
        boost::system::error_code error = boost::asio::error::host_not_found;
        while (error && endpoint_iterator != end)
        {
            socket.close();
            socket.connect(*endpoint_iterator++, error);
        }
        if (error)
            throw boost::system::system_error(error);

        boost::asio::write(socket, boost::asio::buffer(create_sbd_mt_data_message(bytes, imei)));

        directip::SBDMTConfirmationMessageReader message(socket);
        boost::asio::async_read(
            socket, boost::asio::buffer(message.data()),
            boost::asio::transfer_at_least(directip::SBDMessageReader::PRE_HEADER_SIZE),
            boost::bind(&directip::SBDMessageReader::pre_header_handler, &message,
                        boost::placeholders::_1, boost::placeholders::_2));

        double start_time = time::SystemClock::now().time_since_epoch() / std::chrono::seconds(1);
        const int timeout = 5;

        while (!message.data_ready() &&
               (start_time + timeout >
                time::SystemClock::now().time_since_epoch() / std::chrono::seconds(1)))
            io_service.poll();

        if (message.data_ready())
        {
            glog.is(DEBUG1) && glog << "Tx SBD Confirmation: " << message.confirm().DebugString()
                                    << std::endl;
        }
        else
        {
            glog.is(WARN) && glog << "Timeout waiting for confirmation message from DirectIP server"
                                  << std::endl;
        }
    }
    catch (std::exception& e)
    {
        glog.is(WARN) && glog << "Could not sent MT SBD message: " << e.what() << std::endl;
    }
}

std::string goby::acomms::IridiumShoreDriver::create_sbd_mt_data_message(const std::string& bytes,
                                                                         const std::string& imei)
{
    enum
    {
        PRE_HEADER_SIZE = 3,
        BITS_PER_BYTE = 8,
        IEI_SIZE = 3,
        HEADER_SIZE = 21
    };

    enum
    {
        IEI_MT_HEADER = 0x41,
        IEI_MT_PAYLOAD = 0x42
    };

    static int i = 0;
    DirectIPMTHeader header;
    header.set_iei(IEI_MT_HEADER);
    header.set_length(HEADER_SIZE);
    header.set_client_id(i++);
    header.set_imei(imei);

    enum
    {
        DISP_FLAG_FLUSH_MT_QUEUE = 0x01,
        DISP_FLAG_SEND_RING_ALERT_NO_MTM = 0x02,
        DISP_FLAG_UPDATE_SSD_LOCATION = 0x08,
        DISP_FLAG_HIGH_PRIORITY_MESSAGE = 0x10,
        DISP_FLAG_ASSIGN_MTMSN = 0x20
    };

    header.set_disposition_flags(DISP_FLAG_FLUSH_MT_QUEUE);

    std::string header_bytes(IEI_SIZE + HEADER_SIZE, '\0');

    std::string::size_type pos = 0;
    enum
    {
        HEADER_IEI = 1,
        HEADER_LENGTH = 2,
        HEADER_CLIENT_ID = 3,
        HEADER_IMEI = 4,
        HEADER_DISPOSITION_FLAGS = 5
    };

    for (int field = HEADER_IEI; field <= HEADER_DISPOSITION_FLAGS; ++field)
    {
        switch (field)
        {
            case HEADER_IEI: header_bytes[pos++] = header.iei() & 0xff; break;

            case HEADER_LENGTH:
                header_bytes[pos++] = (header.length() >> BITS_PER_BYTE) & 0xff;
                header_bytes[pos++] = (header.length()) & 0xff;
                break;

            case HEADER_CLIENT_ID:
                header_bytes[pos++] = (header.client_id() >> 3 * BITS_PER_BYTE) & 0xff;
                header_bytes[pos++] = (header.client_id() >> 2 * BITS_PER_BYTE) & 0xff;
                header_bytes[pos++] = (header.client_id() >> BITS_PER_BYTE) & 0xff;
                header_bytes[pos++] = (header.client_id()) & 0xff;
                break;

            case HEADER_IMEI:
                header_bytes.replace(pos, 15, header.imei());
                pos += 15;
                break;

            case HEADER_DISPOSITION_FLAGS:
                header_bytes[pos++] = (header.disposition_flags() >> BITS_PER_BYTE) & 0xff;
                header_bytes[pos++] = (header.disposition_flags()) & 0xff;
                break;
        }
    }

    DirectIPMTPayload payload;
    payload.set_iei(IEI_MT_PAYLOAD);
    payload.set_length(bytes.size());
    payload.set_payload(bytes);

    std::string payload_bytes(IEI_SIZE + bytes.size(), '\0');
    payload_bytes[0] = payload.iei();
    payload_bytes[1] = (payload.length() >> BITS_PER_BYTE) & 0xff;
    payload_bytes[2] = (payload.length()) & 0xff;
    payload_bytes.replace(3, payload.payload().size(), payload.payload());

    // Protocol Revision Number (1 byte) == 1
    // Overall Message Length (2 bytes)
    int overall_length = header_bytes.size() + payload_bytes.size();
    std::string pre_header_bytes(PRE_HEADER_SIZE, '\0');
    pre_header_bytes[0] = 1;
    pre_header_bytes[1] = (overall_length >> BITS_PER_BYTE) & 0xff;
    pre_header_bytes[2] = (overall_length)&0xff;

    glog.is(DEBUG1) && glog << "Tx SBD PreHeader: " << goby::util::hex_encode(pre_header_bytes)
                            << std::endl;
    glog.is(DEBUG1) && glog << "Tx SBD Header: " << header.DebugString() << std::endl;
    glog.is(DEBUG1) && glog << "Tx SBD Payload: " << payload.DebugString() << std::endl;

    return pre_header_bytes + header_bytes + payload_bytes;
}
