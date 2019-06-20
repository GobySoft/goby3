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

#include "moos_ufield_sim_driver.h"
#include "goby/acomms/modemdriver/driver_exception.h"
#include "goby/acomms/protobuf/mm_driver.pb.h"
#include "goby/moos/modem_id_convert.h"
#include "goby/moos/moos_string.h"
#include "goby/moos/protobuf/ufield_sim_driver.pb.h"
#include "goby/util/binary.h"
#include "goby/util/debug_logger.h"
#include "goby/util/protobuf/io.h"

using goby::glog;
using goby::util::hex_decode;
using goby::util::hex_encode;
using namespace goby::util::logger;

namespace micromodem = goby::acomms::micromodem;

goby::moos::UFldDriver::UFldDriver() : last_ccmpc_dest_(-1) {}

void goby::moos::UFldDriver::startup(const goby::acomms::protobuf::DriverConfig& cfg)
{
    glog.is(DEBUG1) && glog << group(glog_out_group())
                            << "Goby MOOS uField Toolbox driver starting up." << std::endl;

    driver_cfg_ = cfg;

    goby::glog.is(goby::util::logger::DEBUG1) &&
        goby::glog << modem_lookup_.read_lookup_file(
                          driver_cfg_.GetExtension(protobuf::Config::modem_id_lookup_path))
                   << std::flush;

    // for(int i = 0, n = driver_cfg_.ExtensionSize(protobuf::Config::id_entry);
    //     i < n; ++ i)
    // {
    //     const protobuf::ModemIdEntry& entry =
    //         driver_cfg_.GetExtension(protobuf::Config::id_entry, i);

    //     modem_id2name_.left.insert(std::make_pair(entry.modem_id(), entry.name()));
    // }

    const std::string& moos_server = driver_cfg_.GetExtension(protobuf::Config::moos_server);
    int moos_port = driver_cfg_.GetExtension(protobuf::Config::moos_port);
    moos_client_.Run(moos_server.c_str(), moos_port, "goby.moos.UFldDriver");

    int i = 0;
    while (!moos_client_.IsConnected())
    {
        glog.is(DEBUG1) && glog << group(glog_out_group()) << "Trying to connect to MOOSDB at "
                                << moos_server << ":" << moos_port << ", try " << i++ << std::endl;
        sleep(1);
    }
    glog.is(DEBUG1) && glog << group(glog_out_group()) << "Connected to MOOSDB." << std::endl;

    moos_client_.Register(driver_cfg_.GetExtension(protobuf::Config::incoming_moos_var), 0);
    moos_client_.Register(
        driver_cfg_.GetExtension(protobuf::Config::micromodem_mimic).range_report_var(), 0);
}

void goby::moos::UFldDriver::shutdown() { moos_client_.Close(); }

void goby::moos::UFldDriver::handle_initiate_transmission(
    const goby::acomms::protobuf::ModemTransmission& orig_msg)
{
    // copy so we can modify
    goby::acomms::protobuf::ModemTransmission msg = orig_msg;

    // allows zero to N third parties modify the transmission before sending.
    signal_modify_transmission(&msg);

    switch (msg.type())
    {
        case goby::acomms::protobuf::ModemTransmission::DATA:
        {
            if (driver_cfg_.modem_id() == msg.src())
            {
                // this is our transmission

                if (msg.rate() < driver_cfg_.ExtensionSize(protobuf::Config::rate_to_bytes))
                    msg.set_max_frame_bytes(
                        driver_cfg_.GetExtension(protobuf::Config::rate_to_bytes, msg.rate()));
                else
                    msg.set_max_frame_bytes(DEFAULT_PACKET_SIZE);

                // no data given to us, let's ask for some
                if (msg.frame_size() == 0)
                    ModemDriverBase::signal_data_request(&msg);

                // don't send an empty message
                if (msg.frame_size() && msg.frame(0).size())
                {
                    send_message(msg);
                }
            }
            else
            {
                // send thirdparty "poll"
                msg.SetExtension(goby::moos::protobuf::poll_src, msg.src());
                msg.SetExtension(goby::moos::protobuf::poll_dest, msg.dest());

                msg.set_dest(msg.src());
                msg.set_src(driver_cfg_.modem_id());

                msg.set_type(goby::acomms::protobuf::ModemTransmission::DRIVER_SPECIFIC);
                msg.SetExtension(goby::moos::protobuf::type,
                                 goby::moos::protobuf::UFIELD_DRIVER_POLL);

                send_message(msg);
            }
        }
        break;

        case goby::acomms::protobuf::ModemTransmission::DRIVER_SPECIFIC:
        {
            if (msg.GetExtension(micromodem::protobuf::transmission).has_type())
            {
                switch (msg.GetExtension(micromodem::protobuf::transmission).type())
                {
                    case micromodem::protobuf::MICROMODEM_TWO_WAY_PING: ccmpc(msg); break;
                    default:
                        glog.is(DEBUG1) &&
                            glog << group(glog_out_group()) << warn
                                 << "Not initiating transmission because we were given a "
                                    "DRIVER_SPECIFIC transmission type for the Micro-Modem that "
                                    "isn't supported by the uFld simulator driver"
                                 << msg.ShortDebugString() << std::endl;
                        break;
                }
            }
            else
            {
                glog.is(DEBUG1) && glog << group(glog_out_group()) << warn
                                        << "Not initiating transmission because we were given a "
                                           "missing or unrecognized DRIVER_SPECIFIC type: "
                                        << msg.ShortDebugString() << std::endl;
            }
        }
        break;

        case goby::acomms::protobuf::ModemTransmission::ACK: break;
    }
}

void goby::moos::UFldDriver::send_message(const goby::acomms::protobuf::ModemTransmission& msg)
{
    std::string dest = "unknown";
    if (msg.dest() == acomms::BROADCAST_ID)
        dest = "all";
    else
        dest = modem_lookup_.get_name_from_id(msg.dest());

    std::string src = modem_lookup_.get_name_from_id(msg.src());

    std::string hex = hex_encode(msg.SerializeAsString());

    std::stringstream out_ss;
    out_ss << "src_node=" << src << ",dest_node=" << dest
           << ",var_name=" << driver_cfg_.GetExtension(protobuf::Config::incoming_moos_var)
           << ",string_val=" << hex;

    goby::acomms::protobuf::ModemRaw out_raw;
    out_raw.set_raw(out_ss.str());
    ModemDriverBase::signal_raw_outgoing(out_raw);

    const std::string& out_moos_var = driver_cfg_.GetExtension(protobuf::Config::outgoing_moos_var);

    glog.is(DEBUG1) && glog << group(glog_out_group()) << out_moos_var << ": " << hex << std::endl;

    moos_client_.Notify(out_moos_var, hex);

    const std::string& out_ufield_moos_var =
        driver_cfg_.GetExtension(protobuf::Config::ufield_outgoing_moos_var);

    glog.is(DEBUG1) && glog << group(glog_out_group()) << out_ufield_moos_var << ": "
                            << out_ss.str() << std::endl;

    moos_client_.Notify(out_ufield_moos_var, out_ss.str());
}

void goby::moos::UFldDriver::do_work()
{
    MOOSMSG_LIST msgs;
    if (moos_client_.Fetch(msgs))
    {
        for (MOOSMSG_LIST::iterator it = msgs.begin(), end = msgs.end(); it != end; ++it)
        {
            const std::string& in_moos_var =
                driver_cfg_.GetExtension(protobuf::Config::incoming_moos_var);
            if (it->GetKey() == in_moos_var)
            {
                const std::string& value = it->GetString();

                glog.is(DEBUG1) && glog << group(glog_in_group()) << in_moos_var << ": " << value
                                        << std::endl;
                goby::acomms::protobuf::ModemRaw in_raw;
                in_raw.set_raw(value);
                ModemDriverBase::signal_raw_incoming(in_raw);

                goby::acomms::protobuf::ModemTransmission msg;
                msg.ParseFromString(hex_decode(value));
                receive_message(msg);
            }
            else if (it->GetKey() == driver_cfg_.GetExtension(protobuf::Config::micromodem_mimic)
                                         .range_report_var())
            {
                // response to our modem "ping"
                // e.g "range=1722.3869,target=resolution,time=1364413337.272"
                try
                {
                    goby::acomms::protobuf::ModemTransmission m;
                    std::string target;
                    double range, time;
                    if (!val_from_string(target, it->GetString(), "target"))
                        throw(std::runtime_error("No `target` field"));
                    if (!val_from_string(range, it->GetString(), "range"))
                        throw(std::runtime_error("No `range` field"));
                    if (!val_from_string(time, it->GetString(), "time"))
                        throw(std::runtime_error("No `time` field"));

                    if (target != modem_lookup_.get_name_from_id(last_ccmpc_dest_))
                    {
                        glog.is(DEBUG1) && glog << group(glog_in_group())
                                                << "Ignoring report from target: " << target
                                                << std::endl;
                    }
                    else
                    {
                        m.set_time(static_cast<std::uint64_t>(time) * 1000000);

                        m.set_src(driver_cfg_.modem_id());
                        m.set_dest(last_ccmpc_dest_);

                        micromodem::protobuf::RangingReply* ranging_reply =
                            m.MutableExtension(micromodem::protobuf::transmission)
                                ->mutable_ranging_reply();

                        ranging_reply->add_one_way_travel_time(range / NOMINAL_SPEED_OF_SOUND);

                        m.set_type(goby::acomms::protobuf::ModemTransmission::DRIVER_SPECIFIC);
                        m.MutableExtension(micromodem::protobuf::transmission)
                            ->set_type(micromodem::protobuf::MICROMODEM_TWO_WAY_PING);

                        glog.is(DEBUG1) &&
                            glog << group(glog_in_group())
                                 << "Received mimic of MICROMODEM_TWO_WAY_PING response from "
                                 << m.src() << ", 1-way travel time: "
                                 << ranging_reply->one_way_travel_time(
                                        ranging_reply->one_way_travel_time_size() - 1)
                                 << "s" << std::endl;

                        ModemDriverBase::signal_receive(m);
                        last_ccmpc_dest_ = -1;
                    }
                }
                catch (std::exception& e)
                {
                    glog.is(DEBUG1) && glog << group(glog_in_group()) << warn
                                            << "Failed to parse incoming range report message: "
                                            << e.what() << std::endl;
                }
            }
        }
    }
}

void goby::moos::UFldDriver::receive_message(const goby::acomms::protobuf::ModemTransmission& msg)
{
    if (msg.type() == goby::acomms::protobuf::ModemTransmission::DRIVER_SPECIFIC &&
        msg.GetExtension(goby::moos::protobuf::type) == goby::moos::protobuf::UFIELD_DRIVER_POLL)
    {
        goby::acomms::protobuf::ModemTransmission data_msg = msg;
        data_msg.set_type(goby::acomms::protobuf::ModemTransmission::DATA);
        data_msg.set_src(msg.GetExtension(goby::moos::protobuf::poll_src));
        data_msg.set_dest(msg.GetExtension(goby::moos::protobuf::poll_dest));

        data_msg.ClearExtension(goby::moos::protobuf::type);
        data_msg.ClearExtension(goby::moos::protobuf::poll_dest);
        data_msg.ClearExtension(goby::moos::protobuf::poll_src);

        handle_initiate_transmission(data_msg);
    }
    else
    {
        // ack any packets
        if (msg.type() == acomms::protobuf::ModemTransmission::DATA && msg.ack_requested() &&
            msg.dest() != acomms::BROADCAST_ID)
        {
            goby::acomms::protobuf::ModemTransmission ack;
            ack.set_type(goby::acomms::protobuf::ModemTransmission::ACK);
            ack.set_src(msg.dest());
            ack.set_dest(msg.src());
            for (int i = 0, n = msg.frame_size(); i < n; ++i) ack.add_acked_frame(i);
            send_message(ack);
        }

        ModemDriverBase::signal_receive(msg);
    }
}

// mimics the Micro-Modem ccpmc in uFld
void goby::moos::UFldDriver::ccmpc(const goby::acomms::protobuf::ModemTransmission& msg)
{
    glog.is(DEBUG1) && glog << group(glog_out_group())
                            << "\tthis is a mimic of the MICROMODEM_TWO_WAY_PING transmission"
                            << std::endl;

    std::string src = modem_lookup_.get_name_from_id(msg.src());
    last_ccmpc_dest_ = msg.dest();
    moos_client_.Notify(
        driver_cfg_.GetExtension(protobuf::Config::micromodem_mimic).range_request_var(),
        "name=" + src);
}
