// Copyright 2013-2020:
//   GobySoft, LLC (2013-)
//   Massachusetts Institute of Technology (2007-2014)
//   Community contributors (see AUTHORS file)
// File authors:
//   Toby Schneider <toby@gobysoft.org>
//   Russ Webber <russ@rw.id.au>
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

#include <boost/algorithm/string/classification.hpp>   // for is_any_ofF
#include <boost/algorithm/string/predicate.hpp>        // for all
#include <boost/algorithm/string/predicate_facade.hpp> // for predicate...
#include <boost/algorithm/string/trim.hpp>             // for trim_copy
#include <boost/asio/serial_port.hpp>                  // for serial_port
#include <boost/circular_buffer.hpp>                   // for circular_...
#include <boost/function.hpp>                          // for function
#include <boost/signals2/expired_slot.hpp>             // for expired_slot
#include <boost/signals2/signal.hpp>                   // for signal
#include <boost/statechart/state_machine.hpp>          // for state_mac...
#include <chrono>                                      // for operator/
#include <dccl/common.h>                               // for operator<<
#include <dccl/option_extensions.pb.h>                 // for DCCLField...
#include <errno.h>                                     // for errno
#include <google/protobuf/descriptor.h>                // for Descriptor
#include <google/protobuf/descriptor.pb.h>             // for FieldOptions
#include <list>                                        // for operator!=
#include <ostream>                                     // for operator<<
#include <string.h>                                    // for strerror
#include <string>                                      // for string
#include <sys/ioctl.h>                                 // for ioctl
#include <typeinfo>                                    // for bad_cast
#include <unistd.h>                                    // for usleep

#include "goby/acomms/acomms_constants.h"                  // for BITS_IN_BYTE
#include "goby/acomms/modemdriver/driver_exception.h"      // for ModemDriv...
#include "goby/acomms/modemdriver/iridium_driver_common.h" // for RATE_RUDICS
#include "goby/acomms/modemdriver/rudics_packet.h"         // for serialize...
#include "goby/acomms/protobuf/modem_driver_status.pb.h"   // for ModemDriv...
#include "goby/time/system_clock.h"                        // for SystemClock
#include "goby/util/binary.h"                              // for hex_encode
#include "goby/util/debug_logger/flex_ostream.h"           // for FlexOstream
#include "goby/util/debug_logger/flex_ostreambuf.h"        // for DEBUG1
#include "goby/util/debug_logger/logger_manipulators.h"    // for operator<<
#include "goby/util/linebasedcomms/interface.h"            // for LineBased...
#include "goby/util/linebasedcomms/serial_client.h"        // for SerialClient
#include "goby/util/linebasedcomms/tcp_client.h"           // for TCPClient
#include "goby/util/protobuf/io.h"                         // for operator<<
#include "iridium_driver.h"

namespace dccl
{
class Codec;
} // namespace dccl

using goby::glog;
using namespace goby::util::logger;

std::shared_ptr<dccl::Codec> goby::acomms::iridium_header_dccl_;

goby::acomms::IridiumDriver::IridiumDriver() : fsm_(driver_cfg_)
{
    init_iridium_dccl();

    //    assert(byte_string_to_uint32(uint32_to_byte_string(16540)) == 16540);
}

goby::acomms::IridiumDriver::~IridiumDriver() = default;

void goby::acomms::IridiumDriver::startup(const protobuf::DriverConfig& cfg)
{
    driver_cfg_ = cfg;

    glog.is(DEBUG1) && glog << group(glog_out_group())
                            << "Goby Iridium RUDICS/SBD driver starting up." << std::endl;

    driver_cfg_.set_line_delimiter("\r");

    auto* mutable_iridium_driver_cfg = driver_cfg_.MutableExtension(iridium::protobuf::config);

    if (iridium_driver_cfg().has_debug_client_port())
    {
        debug_client_.reset(new goby::util::TCPClient(
            "localhost", mutable_iridium_driver_cfg->debug_client_port(), "\r"));
        debug_client_->start();
    }

    if (!iridium_driver_cfg().has_use_dtr() &&
        driver_cfg_.connection_type() == protobuf::DriverConfig::CONNECTION_SERIAL)
    {
        mutable_iridium_driver_cfg->set_use_dtr(true);
    }

    // dtr low hangs up
    if (iridium_driver_cfg().use_dtr())
        mutable_iridium_driver_cfg->add_config("&D2");

    rudics_mac_msg_.set_src(driver_cfg_.modem_id());
    rudics_mac_msg_.set_type(goby::acomms::protobuf::ModemTransmission::DATA);
    rudics_mac_msg_.set_rate(RATE_RUDICS);

    modem_init();
}

void goby::acomms::IridiumDriver::modem_init()
{
    modem_start(driver_cfg_);

    fsm_.initiate();

    int i = 0;
    bool dtr_set = false;
    while (fsm_.state_cast<const iridium::fsm::Ready*>() == 0)
    {
        do_work();

        if (iridium_driver_cfg().use_dtr() && modem().active() && !dtr_set)
        {
            serial_fd_ = dynamic_cast<util::SerialClient&>(modem()).socket().native_handle();
            set_dtr(true);
            glog.is(DEBUG1) && glog << group(glog_out_group()) << "DTR is: " << query_dtr()
                                    << std::endl;
            dtr_set = true;
        }

        const int pause_ms = 10;
        usleep(pause_ms * 1000);
        ++i;

        const int start_timeout = iridium_driver_cfg().start_timeout();
        if (i / (1000 / pause_ms) > start_timeout)
            throw(ModemDriverException("Failed to startup.",
                                       protobuf::ModemDriverStatus::STARTUP_FAILED));
    }
}

void goby::acomms::IridiumDriver::set_dtr(bool state)
{
    int status;
    if (ioctl(serial_fd_, TIOCMGET, &status) == -1)
    {
        glog.is(DEBUG1) && glog << group(glog_out_group()) << warn
                                << "IOCTL failed: " << strerror(errno) << std::endl;
    }

    glog.is(DEBUG1) && glog << group(glog_out_group()) << "Setting DTR to "
                            << (state ? "high" : "low") << std::endl;
    if (state)
        status |= TIOCM_DTR;
    else
        status &= ~TIOCM_DTR;
    if (ioctl(serial_fd_, TIOCMSET, &status) == -1)
    {
        glog.is(DEBUG1) && glog << group(glog_out_group()) << warn
                                << "IOCTL failed: " << strerror(errno) << std::endl;
    }
}

bool goby::acomms::IridiumDriver::query_dtr()
{
    int status;
    if (ioctl(serial_fd_, TIOCMGET, &status) == -1)
    {
        glog.is(DEBUG1) && glog << group(glog_out_group()) << warn
                                << "IOCTL failed: " << strerror(errno) << std::endl;
    }
    return (status & TIOCM_DTR);
}

void goby::acomms::IridiumDriver::hangup()
{
    if (iridium_driver_cfg().use_dtr())
    {
        set_dtr(false);
        sleep(1);
        set_dtr(true);
        // phone doesn't give "NO CARRIER" message after DTR disconnect
        fsm_.process_event(iridium::fsm::EvNoCarrier());
    }
    else
    {
        fsm_.process_event(iridium::fsm::EvHangup());
    }
}

void goby::acomms::IridiumDriver::shutdown()
{
    hangup();

    while (fsm_.state_cast<const iridium::fsm::OnCall*>() != 0)
    {
        do_work();
        usleep(10000);
    }

    if (iridium_driver_cfg().use_dtr())
        set_dtr(false);

    modem_close();
}

void goby::acomms::IridiumDriver::handle_initiate_transmission(
    const protobuf::ModemTransmission& orig_msg)
{
    process_transmission(orig_msg, true);
}

void goby::acomms::IridiumDriver::process_transmission(protobuf::ModemTransmission msg, bool dial)
{
    signal_modify_transmission(&msg);

    const static unsigned frame_max = iridium::protobuf::IridiumHeader::descriptor()
                                          ->FindFieldByName("frame_start")
                                          ->options()
                                          .GetExtension(dccl::field)
                                          .max();

    if (!msg.has_frame_start())
        msg.set_frame_start(next_frame_ % frame_max);

    // set the frame size, if not set or if it exceeds the max configured
    if (!msg.has_max_frame_bytes() || msg.max_frame_bytes() > iridium_driver_cfg().max_frame_size())
        msg.set_max_frame_bytes(iridium_driver_cfg().max_frame_size());

    signal_data_request(&msg);

    next_frame_ += msg.frame_size();

    if (!(msg.frame_size() == 0 || msg.frame(0).empty()))
    {
        if (dial && msg.rate() == RATE_RUDICS)
            fsm_.process_event(iridium::fsm::EvDial());
        send(msg);
    }
    else if (msg.rate() == RATE_SBD)
    {
        if (msg.GetExtension(iridium::protobuf::transmission).if_no_data_do_mailbox_check())
            fsm_.process_event(iridium::fsm::EvSBDBeginData()); // mailbox check
    }
}

void goby::acomms::IridiumDriver::do_work()
{
    //   if(glog.is(DEBUG1))
    //    display_state_cfg(&glog);
    double now = time::SystemClock::now().time_since_epoch() / std::chrono::seconds(1);

    const auto* on_call = fsm_.state_cast<const iridium::fsm::OnCall*>();

    if (on_call)
    {
        // if we're on a call, keep pushing data at the target rate
        const double send_wait =
            on_call->last_bytes_sent() /
            (iridium_driver_cfg().target_bit_rate() / static_cast<double>(BITS_IN_BYTE));

        if (fsm_.data_out().empty() && (now > (on_call->last_tx_time() + send_wait)))
        {
            if (!on_call->bye_sent())
            {
                process_transmission(rudics_mac_msg_, false);
            }
        }

        if (!on_call->bye_sent() &&
            now > (on_call->last_tx_time() + iridium_driver_cfg().handshake_hangup_seconds()))
        {
            glog.is(DEBUG2) && glog << "Sending bye" << std::endl;
            fsm_.process_event(iridium::fsm::EvSendBye());
        }

        if ((on_call->bye_received() && on_call->bye_sent()) ||
            (now >
             (on_call->last_rx_tx_time() + iridium_driver_cfg().hangup_seconds_after_empty())))
        {
            hangup();
        }
    }

    try_serial_tx();

    std::string in;
    while (modem().active() && modem_read(&in))
    {
        iridium::fsm::EvRxSerial data_event;
        data_event.line = in;

        glog.is(DEBUG1) &&
            glog << group(glog_in_group())
                 << (boost::algorithm::all(in, boost::is_print() || boost::is_any_of("\r\n"))
                         ? boost::trim_copy(in)
                         : goby::util::hex_encode(in))
                 << std::endl;

        if (debug_client_ && fsm_.state_cast<const iridium::fsm::OnCall*>() != 0)
        {
            debug_client_->write(in);
        }

        fsm_.process_event(data_event);
    }

    while (!fsm_.received().empty())
    {
        receive(fsm_.received().front());
        fsm_.received().pop_front();
    }

    if (debug_client_)
    {
        std::string line;
        while (debug_client_->readline(&line))
        {
            fsm_.serial_tx_buffer().push_back(line);
            fsm_.process_event(iridium::fsm::EvDial());
        }
    }

    // try sending again at the end to push newly generated messages before we wait
    try_serial_tx();
}

void goby::acomms::IridiumDriver::receive(const protobuf::ModemTransmission& msg)
{
    glog.is(DEBUG2) && glog << group(glog_in_group()) << msg << std::endl;

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

void goby::acomms::IridiumDriver::send(const protobuf::ModemTransmission& msg)
{
    glog.is(DEBUG2) && glog << group(glog_out_group()) << msg << std::endl;

    if (msg.rate() == RATE_RUDICS)
        fsm_.buffer_data_out(msg);
    else if (msg.rate() == RATE_SBD)
    {
        bool on_call = (fsm_.state_cast<const iridium::fsm::OnCall*>() != 0);
        if (on_call) // if we're on a call send it via the call
            fsm_.buffer_data_out(msg);
        else
        {
            std::string iridium_packet;
            serialize_iridium_modem_message(&iridium_packet, msg);

            std::string rudics_packet;
            serialize_rudics_packet(iridium_packet, &rudics_packet);
            fsm_.process_event(iridium::fsm::EvSBDBeginData(rudics_packet));
        }
    }
}

void goby::acomms::IridiumDriver::try_serial_tx()
{
    fsm_.process_event(iridium::fsm::EvTxSerial());

    while (!fsm_.serial_tx_buffer().empty())
    {
        double now = time::SystemClock::now().time_since_epoch() / std::chrono::seconds(1);
        if (last_triple_plus_time_ + TRIPLE_PLUS_WAIT > now)
            return;

        const std::string& line = fsm_.serial_tx_buffer().front();

        glog.is(DEBUG1) &&
            glog << group(glog_out_group())
                 << (boost::algorithm::all(line, boost::is_print() || boost::is_any_of("\r\n"))
                         ? boost::trim_copy(line)
                         : goby::util::hex_encode(line))
                 << std::endl;

        modem_write(line);

        // this is safe as all other messages we use are \r terminated
        if (line == "+++")
            last_triple_plus_time_ = now;

        fsm_.serial_tx_buffer().pop_front();
    }
}

void goby::acomms::IridiumDriver::display_state_cfg(std::ostream* os)
{
    char orthogonalRegion = 'a';

    for (iridium::fsm::IridiumDriverFSM::state_iterator pLeafState = fsm_.state_begin();
         pLeafState != fsm_.state_end(); ++pLeafState)
    {
        *os << "Orthogonal region " << orthogonalRegion << ": ";

        const iridium::fsm::IridiumDriverFSM::state_base_type* pState = &*pLeafState;

        while (pState != nullptr)
        {
            if (pState != &*pLeafState)
            {
                *os << " -> ";
            }

            std::string name = typeid(*pState).name();
            std::string prefix = "N4goby6acomms3fsm";
            std::string suffix = "E";

            if (name.find(prefix) != std::string::npos && name.find(suffix) != std::string::npos)
                name =
                    name.substr(prefix.size() + 1, name.size() - prefix.size() - suffix.size() - 1);

            *os << name;

            pState = pState->outer_state_ptr();
        }

        *os << "\n";
        ++orthogonalRegion;
    }

    *os << std::endl;
}
