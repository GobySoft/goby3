// Copyright 2011-2020:
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

#include "abc_driver.h"
#include "driver_exception.h"

#include "goby/util/binary.h"
#include "goby/util/debug_logger.h"
#include "goby/util/protobuf/io.h"

using goby::glog;
using goby::util::hex_decode;
using goby::util::hex_encode;
using namespace goby::util::logger;

goby::acomms::ABCDriver::ABCDriver()
{
    // other initialization you can do before you have your goby::acomms::DriverConfig configuration object
}

void goby::acomms::ABCDriver::startup(const protobuf::DriverConfig& cfg)
{
    driver_cfg_ = cfg;
    // check `driver_cfg_` to your satisfaction and then start the modem physical interface
    if (!driver_cfg_.has_serial_baud())
        driver_cfg_.set_serial_baud(DEFAULT_BAUD);

    glog.is(DEBUG1) && glog << group(glog_out_group())
                            << "ABCDriver configuration good. Starting modem..." << std::endl;
    ModemDriverBase::modem_start(driver_cfg_);

    // set your local modem id (MAC address)
    {
        std::stringstream raw;
        raw << "CONF,MAC:" << driver_cfg_.modem_id() << "\r\n";
        signal_and_write(raw.str());
    }

    // now set our special configuration values
    {
        std::stringstream raw;
        raw << "CONF,FOO:"
            << driver_cfg_.GetExtension(goby::acomms::abc::protobuf::config).enable_foo() << "\r\n";
        signal_and_write(raw.str());
    }
    {
        std::stringstream raw;
        raw << "CONF,BAR:"
            << driver_cfg_.GetExtension(goby::acomms::abc::protobuf::config).enable_bar() << "\r\n";
        signal_and_write(raw.str());
    }
} // startup

void goby::acomms::ABCDriver::shutdown()
{
    // put the modem in a low power state?
    // ...
    ModemDriverBase::modem_close();
} // shutdown

void goby::acomms::ABCDriver::handle_initiate_transmission(
    const protobuf::ModemTransmission& orig_msg)
{
    // copy so we can modify
    protobuf::ModemTransmission msg = orig_msg;

    // rate() can be 0 (lowest), 1, 2, 3, 4, or 5 (lowest). Map these integers onto real bit-rates
    // in a meaningful way (on the WHOI Micro-Modem 0 ~= 80 bps, 5 ~= 5000 bps).
    glog.is(DEBUG1) && glog << group(glog_out_group()) << "We were asked to transmit from "
                            << msg.src() << " to " << msg.dest() << " at bitrate code "
                            << msg.rate() << std::endl;

    // let's say ABC modem uses 500 byte packet
    msg.set_max_frame_bytes(500);

    // no data given to us, let's ask for some
    if (msg.frame_size() == 0)
        ModemDriverBase::signal_data_request(&msg);

    glog.is(DEBUG1) && glog << group(glog_out_group()) << "Sending these data now: " << msg.frame(0)
                            << std::endl;

    // let's say we can send at three bitrates with ABC modem: map these onto 0-5
    const unsigned BITRATE[] = {100, 1000, 10000, 10000, 10000, 10000};

    // I'm making up a syntax for the wire protocol...
    std::stringstream raw;
    raw << "SEND,TO:" << msg.dest() << ",FROM:" << msg.src() << ",HEX:" << hex_encode(msg.frame(0))
        << ",BITRATE:" << BITRATE[msg.rate()] << ",ACK:TRUE"
        << "\r\n";

    // let anyone who is interested know
    signal_and_write(raw.str());
} // handle_initiate_transmission

void goby::acomms::ABCDriver::do_work()
{
    std::string in;
    while (modem_read(&in))
    {
        std::map<std::string, std::string> parsed;

        // breaks `in`: "RECV,TO:3,FROM:6,HEX:ABCD015910"
        //   into `parsed`: "KEY"=>"RECV", "TO"=>"3", "FROM"=>"6", "HEX"=>"ABCD015910"
        try
        {
            boost::trim(in); // get whitespace off from either end
            parse_in(in, &parsed);

            // let others know about the raw feed
            protobuf::ModemRaw raw;
            raw.set_raw(in);
            ModemDriverBase::signal_raw_incoming(raw);

            protobuf::ModemTransmission msg;
            msg.set_src(goby::util::as<std::int32_t>(parsed["FROM"]));
            msg.set_dest(goby::util::as<std::int32_t>(parsed["TO"]));
            msg.set_time_with_units(time::SystemClock::now<time::MicroTime>());

            glog.is(DEBUG1) && glog << group(glog_in_group()) << in << std::endl;

            if (parsed["KEY"] == "RECV")
            {
                msg.set_type(protobuf::ModemTransmission::DATA);
                msg.add_frame(hex_decode(parsed["HEX"]));
                glog.is(DEBUG1) && glog << group(glog_in_group()) << "received: " << msg
                                        << std::endl;
            }
            else if (parsed["KEY"] == "ACKN")
            {
                msg.set_type(protobuf::ModemTransmission::ACK);
            }

            ModemDriverBase::signal_receive(msg);
        }
        catch (std::exception& e)
        {
            glog.is(WARN) && glog << "Bad line: " << in << std::endl;
            glog.is(WARN) && glog << "Exception: " << e.what() << std::endl;
        }
    }
} // do_work

void goby::acomms::ABCDriver::signal_and_write(const std::string& raw)
{
    protobuf::ModemRaw raw_msg;
    raw_msg.set_raw(raw);
    ModemDriverBase::signal_raw_outgoing(raw_msg);

    glog.is(DEBUG1) && glog << group(glog_out_group()) << boost::trim_copy(raw) << std::endl;
    ModemDriverBase::modem_write(raw);
}

void goby::acomms::ABCDriver::parse_in(const std::string& in,
                                       std::map<std::string, std::string>* out)
{
    std::vector<std::string> comma_split;
    boost::split(comma_split, in, boost::is_any_of(","));
    out->insert(std::make_pair("KEY", comma_split.at(0)));
    for (int i = 1, n = comma_split.size(); i < n; ++i)
    {
        std::vector<std::string> colon_split;
        boost::split(colon_split, comma_split[i], boost::is_any_of(":"));
        out->insert(std::make_pair(colon_split.at(0), colon_split.at(1)));
    }
}
