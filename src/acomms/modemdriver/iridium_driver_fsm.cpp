// Copyright 2013-2025:
//   GobySoft, LLC (2013-)
//   Massachusetts Institute of Technology (2007-2014)
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

#include <algorithm> // for min, max
#include <boost/circular_buffer.hpp>
#include <chrono>      // for operator/, seconds, tim...
#include <memory>      // for allocator_traits<>::val...
#include <type_traits> // for __decay_and_strip<>::__...
#include <unistd.h>    // for sleep

#include "goby/acomms/acomms_constants.h" // for BITS_IN_BYTE
#include "goby/time/system_clock.h"       // for SystemClock, SystemCloc...
#include "iridium_driver_fsm.h"
#include "iridium_rudics_packet.h" // for parse_rudics_packet
#include "iridium_sbd_packet.h"    // for parse_sbd_packet

using goby::glog;
using namespace goby::util::logger;

int goby::acomms::iridium::fsm::IridiumDriverFSM::count_ = 0;

void goby::acomms::iridium::fsm::IridiumDriverFSM::buffer_data_out(
    const goby::acomms::protobuf::ModemTransmission& msg)
{
    data_out_.push_back(msg);
}

void goby::acomms::iridium::fsm::IridiumDriverFSM::parse_ciev(const std::string& ciev)
{
    // expecting +CIEV,0,N (signal quality, N = 0-5)
    // or +CIEV,1,M (service availability, M = 0 or 1)

    enum
    {
        MODE_SIGNAL_QUALITY = 0,
        MODE_SERVICE_AVAILABILITY = 1
    };

    auto colon_pos = ciev.find(':');
    auto comma_pos = ciev.find(',');

    // Check if both characters were found and the prefix is correct
    if (colon_pos != std::string::npos && comma_pos != std::string::npos &&
        ciev.substr(0, colon_pos) == "+CIEV")
    {
        try
        {
            // Extract the substrings for the mode and value
            std::string mode_str = ciev.substr(colon_pos + 1, comma_pos - colon_pos - 1);
            std::string value_str = ciev.substr(comma_pos + 1);

            // Convert the substrings to integers
            int mode = std::stoi(mode_str);
            int value = std::stoi(value_str);

            if (mode == MODE_SIGNAL_QUALITY)
            {
                ciev_data_.rssi = value;
            }
            else if (mode == MODE_SERVICE_AVAILABILITY)
            {
                ciev_data_.service_available = (value == 1);
            }
        }
        catch (const std::exception& e)
        {
            glog.is(DEBUG1) && glog << group("iridiumdriver") << warn
                                    << "Invalid +CIEV: could not convert integers: " << ciev
                                    << std::endl;
        }
    }
    else
    {
        glog.is(DEBUG1) && glog << group("iridiumdriver") << warn << "Invalid +CIEV: " << ciev
                                << std::endl;
    }
}

void goby::acomms::iridium::fsm::IridiumDriverFSM::sbd_session_failed()
{
    const auto& cfg = iridium_driver_cfg();

    double backoff = cfg.sbd_retry_backoff_initial();
    for (int i = 0; i < sbd_consecutive_failures_ && backoff < cfg.sbd_retry_backoff_max(); ++i)
        backoff *= 2;
    backoff = std::min<double>(backoff, cfg.sbd_retry_backoff_max());
    ++sbd_consecutive_failures_;

    std::uniform_real_distribution<double> jitter(0,
                                                  std::max(0.0, cfg.sbd_retry_jitter_fraction()));
    backoff *= 1 + jitter(rng_);

    sbd_blocked_until_ = goby::time::SteadyClock::now() +
                         std::chrono::duration_cast<goby::time::SteadyClock::duration>(
                             std::chrono::duration<double>(backoff));
    sbd_block_reason_ = SBDBlockReason::RETRY_BACKOFF;

    glog.is(DEBUG1) &&
        glog << group("iridiumdriver") << "SBD session failed (" << sbd_consecutive_failures_
             << " consecutive); next session allowed in " << backoff << " s" << std::endl;
}

void goby::acomms::iridium::fsm::IridiumDriverFSM::sbd_traffic_management(
    std::chrono::seconds remaining)
{
    auto tm_end = goby::time::SteadyClock::now() + remaining;
    if (tm_end > sbd_blocked_until_)
    {
        sbd_blocked_until_ = tm_end;
        sbd_block_reason_ = SBDBlockReason::TRAFFIC_MANAGEMENT;
    }

    glog.is(WARN) && glog << group("iridiumdriver") << "SBD traffic management active for "
                          << remaining.count() << " s; no SBD sessions until it ends" << std::endl;
}

void goby::acomms::iridium::fsm::IridiumDriverFSM::parse_sbdloe(const std::string& sbdloe)
{
    // +SBDLOE:<status>,<time>
    auto colon_pos = sbdloe.find(':');
    auto comma_pos = sbdloe.find(',');
    if (colon_pos == std::string::npos || comma_pos == std::string::npos)
    {
        glog.is(WARN) && glog << group("iridiumdriver") << "Invalid +SBDLOE: " << sbdloe
                              << std::endl;
        return;
    }

    enum
    {
        SBDLOE_TIME_VALID = 0
    };

    try
    {
        int status = std::stoi(sbdloe.substr(colon_pos + 1, comma_pos - colon_pos - 1));
        int time = std::stoi(sbdloe.substr(comma_pos + 1));

        if (status != SBDLOE_TIME_VALID)
            glog.is(DEBUG1) && glog << group("iridiumdriver")
                                    << "Traffic management time could not be determined"
                                    << std::endl;
        else if (time > 0)
            sbd_traffic_management(std::chrono::seconds(time));
    }
    catch (const std::exception& e)
    {
        glog.is(WARN) && glog << group("iridiumdriver")
                              << "Invalid +SBDLOE: could not convert integers: " << sbdloe
                              << std::endl;
    }
}

int goby::acomms::iridium::fsm::IridiumDriverFSM::parse_csq(const std::string& csq)
{
    // +CSQ:<rssi>
    auto colon_pos = csq.find(':');
    try
    {
        if (colon_pos != std::string::npos)
        {
            int rssi = std::stoi(csq.substr(colon_pos + 1));
            ciev_data_.rssi = rssi;
            return rssi;
        }
    }
    catch (const std::exception& e)
    {
    }

    glog.is(WARN) && glog << group("iridiumdriver") << "Invalid +CSQ: " << csq << std::endl;
    return 0;
}

void goby::acomms::iridium::fsm::Command::in_state_react(const EvRxSerial& e)
{
    std::string in = e.line;

    // deal with SBD received data special case
    if (!at_out().empty() && at_out().front().second == "+SBDRB")
    {
        handle_sbd_rx(in);
        return;
    }

    boost::trim(in);

    // deal with echo getting turned back on unintentionally
    if (!at_out().empty() && at_out().front().second != "E" &&
        (in == std::string("AT" + at_out().front().second)))
    {
        glog.is(WARN) && glog << group("iridiumdriver") << "Echo turned on. Disabling" << std::endl;
        // push to front so we send this before anything else
        at_out_.insert(at_out_.begin() + 1, std::make_pair(ATSentenceMeta(), "E"));
        return;
    }

    static const std::string connect = "CONNECT";
    static const std::string sbdi = "+SBDI";
    static const std::string ciev = "+CIEV";
    static const std::string csq = "+CSQ:";
    static const std::string sbdloe = "+SBDLOE:";

    if (in == "OK")
    {
        post_event(EvAck(in));
    }
    else if (in == "RING")
    {
        post_event(EvRing());
    }
    else if (in == "NO CARRIER")
    {
        post_event(EvAck(in));
        post_event(EvNoCarrier());
    }
    else if (in.compare(0, connect.size(), connect) == 0)
    {
        post_event(EvAck(in));
        post_event(EvConnect());
    }
    else if (in == "NO DIALTONE")
    {
        post_event(EvAck(in));
        post_event(EvNoCarrier());
    }
    else if (in == "BUSY")
    {
        post_event(EvAck(in));
        post_event(EvNoCarrier());
    }
    else if (in == "ERROR" && !at_out().empty() && is_optional_command(at_out().front().second))
    {
        // not supported by all modems or firmware versions
        glog.is(WARN) && glog << group("iridiumdriver") << "AT" << at_out().front().second
                              << " not supported by modem" << std::endl;
        const bool sbdloe = at_out().front().second == "+SBDLOE";
        post_event(EvAck(in));
        if (sbdloe)
            post_event(EvSBDTrafficManagementStatus());
    }
    else if (in == "ERROR")
    {
        post_event(EvReset());
    }
    else if (in == "0" || in == "1" || in == "2" || in == "3")
    {
        post_event(EvAck(in));
    }
    else if (in == "READY")
    {
        post_event(EvAck(in));
    }
    else if (in.compare(0, sbdi.size(), sbdi) == 0)
    {
        post_event(EvSBDTransmitComplete(in));
    }
    else if (in == "SBDRING")
    {
        post_event(EvSBDCheckSignal(true));
    }
    else if (in.compare(0, ciev.size(), ciev) == 0)
    {
        context<IridiumDriverFSM>().parse_ciev(in);
    }
    else if (in.compare(0, csq.size(), csq) == 0)
    {
        post_event(EvSBDSignalStrength(context<IridiumDriverFSM>().parse_csq(in)));
    }
    else if (in.compare(0, sbdloe.size(), sbdloe) == 0)
    {
        context<IridiumDriverFSM>().parse_sbdloe(in);
        post_event(EvSBDTrafficManagementStatus());
    }
}

void goby::acomms::iridium::fsm::Command::handle_sbd_rx(const std::string& in)
{
    enum
    {
        SBD_FIELD_SIZE_BYTES = 2,
        SBD_BITS_IN_BYTE = 8,
        SBD_CHECKSUM_BYTES = 2
    };

    if (sbd_rx_buffer_.empty() && in.at(0) == '\n')
        sbd_rx_buffer_ = in.substr(1); // discard left over '\n' from last command
    else
        sbd_rx_buffer_ += in;

    // need to build up message in pieces since we use \r delimiter
    if (sbd_rx_buffer_.size() < SBD_FIELD_SIZE_BYTES)
        return;
    else
    {
        unsigned sbd_rx_size =
            ((sbd_rx_buffer_[0] & 0xff) << SBD_BITS_IN_BYTE) | (sbd_rx_buffer_[1] & 0xff);
        glog.is(DEBUG1) && glog << group("iridiumdriver") << "SBD RX Size: " << sbd_rx_size
                                << std::endl;

        if (sbd_rx_buffer_.size() < (SBD_FIELD_SIZE_BYTES + sbd_rx_size + SBD_CHECKSUM_BYTES))
        {
            return; // keep building up message
        }
        else
        {
            std::string sbd_rx_data = sbd_rx_buffer_.substr(SBD_FIELD_SIZE_BYTES, sbd_rx_size);
            const auto csum_pos = SBD_FIELD_SIZE_BYTES + sbd_rx_size;
            unsigned rx_csum = ((sbd_rx_buffer_[csum_pos] & 0xff) << SBD_BITS_IN_BYTE) |
                               (sbd_rx_buffer_[csum_pos + 1] & 0xff);

            if (rx_csum != (sbd_csum(sbd_rx_data) & 0xFFFF))
            {
                glog.is(WARN) && glog << group("iridiumdriver")
                                      << "Discarding SBD message with invalid +SBDRB checksum"
                                      << std::endl;
            }
            else
            {
                std::string bytes;
                parse_sbd_packet(&bytes, sbd_rx_data);
                goby::acomms::protobuf::ModemTransmission msg;
                parse_iridium_modem_message(bytes, &msg);
                context<IridiumDriverFSM>().received().push_back(msg);
            }
            at_out().pop_front();

            post_event(EvSBDReceiveComplete());

            // so the "OK" that follows has a command to clear
            push_at_command("");
        }
    }
}

void goby::acomms::iridium::fsm::Command::in_state_react(const EvTxSerial&)
{
    double now = time::SystemClock::now().time_since_epoch() / std::chrono::seconds(1);

    if (!at_out_.empty())
    {
        double timeout = COMMAND_TIMEOUT_SECONDS;
        switch (at_out_.front().second[0])
        {
            default: break;
            case 'D': timeout = DIAL_TIMEOUT_SECONDS; break;
            case 'A': timeout = ANSWER_TIMEOUT_SECONDS; break;
            case 'H': timeout = HANGUP_TIMEOUT_SECONDS; break;
            case '+':
                if (at_out_.front().second == "+++")
                    timeout = TRIPLE_PLUS_TIMEOUT_SECONDS;
                break;
        }

        static const std::string sbdi = "+SBDI";
        const bool is_sbd_session = at_out_.front().second.compare(0, sbdi.size(), sbdi) == 0;
        if (is_sbd_session)
            timeout = sbd_session_timeout_seconds(context<IridiumDriverFSM>().iridium_driver_cfg());
        else if (at_out_.front().second == "+CSQ")
            timeout = CSQ_TIMEOUT_SECONDS;
        else if (at_out_.front().second == "+SBDLOE")
            timeout = SBDLOE_TIMEOUT_SECONDS;
        else if (at_out_.front().second == "*F")
            timeout = FLUSH_EEPROM_TIMEOUT_SECONDS;

        if ((at_out_.front().first.last_send_time_ + timeout) < now)
        {
            // resending would start another SBD session without any back-off
            if (is_sbd_session && at_out_.front().first.tries_ > 0)
            {
                at_out_.pop_front();
                post_event(EvSBDSessionTimeout());
                if (at_out_.empty())
                    post_event(EvAtEmpty());
                return;
            }

            std::string at_command;
            if (at_out_.front().second != "+++")
                at_command = "AT" + at_out_.front().second + "\r";
            else
                at_command = at_out_.front().second;

            if (++at_out_.front().first.tries_ > RETRIES_BEFORE_RESET)
            {
                glog.is(DEBUG1) && glog << group("iridiumdriver") << warn
                                        << "No valid response after " << RETRIES_BEFORE_RESET
                                        << " tries. Resetting state machine" << std::endl;
                context<IridiumDriverFSM>().set_modem_not_responding(true);
                post_event(EvReset());
            }
            else
            {
                // only on (re)send, as the reply may arrive over several do_work() calls
                if (at_out_.front().second == "+SBDRB")
                    clear_sbd_rx_buffer();

                context<IridiumDriverFSM>().serial_tx_buffer().push_back(at_command);
                at_out_.front().first.last_send_time_ = now;
            }
        }
    }
}

void goby::acomms::iridium::fsm::Online::in_state_react(const EvRxSerial& e)
{
    EvRxOnCallSerial eo;
    eo.line = e.line;
    post_event(eo);
}

void goby::acomms::iridium::fsm::Online::in_state_react(const EvTxSerial&)
{
    post_event(EvTxOnCallSerial());
}

void goby::acomms::iridium::fsm::Command::in_state_react(const EvAck& e)
{
    // deal with the numeric codes
    if (e.response_.size() > 0)
    {
        switch (e.response_[0])
        {
            case '0':
                if (!at_out().empty() && at_out().front().second == "+SBDD2")
                {
                    post_event(EvSBDSendBufferCleared());
                }
                else if (at_out().empty()) // no AT command before this - we write the data directly
                {
                    post_event(EvSBDWriteComplete());
                    push_at_command("AT"); // this is so the "OK" will have something to clear
                }
                return; // all followed by "OK" which will clear the sentence
            case '1':
            case '2':
            case '3':
            {
                const int result = e.response_[0] - '0';
                if (!at_out().empty() && at_out().front().second == "+SBDD2")
                {
                    post_event(EvSBDSendBufferClearFailed());
                }
                else if (at_out().empty() && state_downcast<const SBDWrite*>() != nullptr)
                {
                    post_event(EvSBDWriteFailed(result));
                    // +SBDWB result 1 (timeout) is the only one not followed by "OK"
                    const int sbdwb_timeout = 1;
                    if (result != sbdwb_timeout)
                        push_at_command("AT");
                }
                return;
            }
            default: break;
        }
    }

    if (!at_out().empty())
    {
        const std::string& last_at = at_out().front().second;
        if (last_at.size() > 0 && (e.response_ == "OK"))
        {
            switch (last_at[0])
            {
                case 'H':
                    post_event(EvNoCarrier());
                    break;

                    // 2015-08-18 - Iridium 9523 gave "OK" in response to a dial (as failure?)
                    // if this happens, assume it's a failure.
                case 'D': post_event(EvNoCarrier()); break;

                default: break;
            }
        }

        if (e.response_ == "READY") // used for SBD
            post_event(EvSBDWriteReady());

        at_out().pop_front();
        if (at_out().empty())
            post_event(EvAtEmpty());
    }
    else
    {
        glog.is(DEBUG1) && glog << group("iridiumdriver") << warn << "Unexpected '" << e.response_
                                << "'" << std::endl;
    }
}

boost::statechart::result goby::acomms::iridium::fsm::Dial::react(const EvNoCarrier& /*x*/)
{
    const int redial_wait_seconds = 2;
    glog.is(DEBUG1) && glog << group("iridiumdriver") << "Redialing in " << redial_wait_seconds
                            << " seconds ..." << std::endl;

    sleep(redial_wait_seconds);

    const int max_attempts = context<IridiumDriverFSM>().iridium_driver_cfg().dial_attempts();
    if (dial_attempts_ < max_attempts)
    {
        dial();
        return discard_event();
    }
    else
    {
        glog.is(DEBUG1) && glog << warn << group("iridiumdriver") << "Failed to connect after "
                                << max_attempts << " tries." << std::endl;

        return transit<Ready>();
    }
}

void goby::acomms::iridium::fsm::Dial::dial()
{
    ++dial_attempts_;
    context<Command>().push_at_command(
        "D" + context<IridiumDriverFSM>().iridium_driver_cfg().remote().iridium_number());
}

void goby::acomms::iridium::fsm::OnCall::in_state_react(const EvRxOnCallSerial& e)
{
    std::string in = e.line;

    // check that it's not "NO CARRIER"
    static const std::string no_carrier = "NO CARRIER";
    if (in.find(no_carrier) != std::string::npos)
    {
        post_event(EvNoCarrier());
    }
    else if (boost::trim_copy(in) == "goby")
    {
        glog.is(DEBUG1) && glog << group("iridiumdriver") << "Detected start of Goby RUDICS call"
                                << std::endl;
    }
    else if (boost::trim_copy(in) == "bye")
    {
        glog.is(DEBUG1) && glog << group("iridiumdriver")
                                << "Detected remote completion of Goby RUDICS call" << std::endl;
        set_bye_received(true);
    }
    else
    {
        std::string bytes;
        try
        {
            parse_rudics_packet(&bytes, in);

            goby::acomms::protobuf::ModemTransmission msg;
            parse_iridium_modem_message(bytes, &msg);
            context<IridiumDriverFSM>().received().push_back(msg);
            set_last_rx_time(time::SystemClock::now().time_since_epoch() / std::chrono::seconds(1));
        }
        catch (RudicsPacketException& e)
        {
            glog.is(DEBUG1) && glog << warn << group("iridiumdriver")
                                    << "Could not decode packet: " << e.what() << std::endl;
        }
    }
}

void goby::acomms::iridium::fsm::OnCall::in_state_react(const EvTxOnCallSerial&)
{
    const static double target_byte_rate =
        (context<IridiumDriverFSM>().iridium_driver_cfg().target_bit_rate() /
         static_cast<double>(goby::acomms::BITS_IN_BYTE));

    const double send_wait = last_bytes_sent() / target_byte_rate;

    double now = time::SystemClock::now().time_since_epoch() / std::chrono::seconds(1);
    boost::circular_buffer<goby::acomms::protobuf::ModemTransmission>& data_out =
        context<IridiumDriverFSM>().data_out();
    if (!data_out.empty() && (now > last_tx_time() + send_wait))
    {
        // serialize the (protobuf) message
        std::string bytes;
        serialize_iridium_modem_message(&bytes, data_out.front());

        // frame message
        std::string rudics_packet;
        serialize_rudics_packet(bytes, &rudics_packet);

        context<IridiumDriverFSM>().serial_tx_buffer().push_back(rudics_packet);
        data_out.pop_front();

        set_last_bytes_sent(rudics_packet.size());
        set_last_tx_time(now);
    }
}

void goby::acomms::iridium::fsm::OnCall::in_state_react(const EvSendBye&)
{
    context<IridiumDriverFSM>().serial_tx_buffer().push_front("bye\r");
    set_bye_sent(true);
}

void goby::acomms::iridium::fsm::SBDWrite::in_state_react(const EvSBDCheckWriteTimeout&)
{
    enum
    {
        SBDWRITE_TIMEOUT_SECONDS = 4
    };
    auto now = goby::time::SteadyClock::now();
    if (now > (entry_time_ + std::chrono::seconds(SBDWRITE_TIMEOUT_SECONDS)))
    {
        glog.is(goby::util::logger::WARN) && glog << group("iridiumdriver")
                                                  << "Timeout waiting for response for SBDWrite"
                                                  << std::endl;
        post_event(
            EvSBDWriteComplete()); // Assume maybe our write worked, or at least do a mailbox check
    }
}
