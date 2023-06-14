// Copyright 2009-2023:
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

#include <boost/bind.hpp>                                   // for bind_t, arg
#include <boost/date_time/posix_time/posix_time_config.hpp> // for posix_time
#include <boost/date_time/posix_time/posix_time_types.hpp>  // for second_c...
#include <boost/date_time/posix_time/time_formatters.hpp>   // for to_iso_s...
#include <boost/format.hpp>                                 // for basic_al...
#include <boost/lexical_cast/bad_lexical_cast.hpp>          // for bad_lexi...
#include <fstream>                                          // for basic_os...
#include <list>                                             // for operator!=
#include <unistd.h>                                         // for usleep

#include "driver_base.h"
#include "driver_exception.h"                            // for ModemDri...
#include "goby/acomms/connect.h"                         // for connect
#include "goby/acomms/protobuf/modem_driver_status.pb.h" // for ModemDri...
#include "goby/acomms/protobuf/modem_message.pb.h"       // for ModemRaw
#include "goby/util/as.h"                                // for as
#include "goby/util/debug_logger/flex_ostream.h"         // for FlexOstream
#include "goby/util/debug_logger/flex_ostreambuf.h"      // for DEBUG1
#include "goby/util/debug_logger/logger_manipulators.h"  // for operator<<
#include "goby/util/debug_logger/term_color.h"           // for Colors
#include "goby/util/linebasedcomms/interface.h"          // for LineBase...
#include "goby/util/linebasedcomms/serial_client.h"      // for SerialCl...
#include "goby/util/linebasedcomms/tcp_client.h"         // for TCPClient
#include "goby/util/linebasedcomms/tcp_server.h"         // for TCPServer

using namespace goby::util::logger;
using namespace goby::util::logger_lock;

std::atomic<int> goby::acomms::ModemDriverBase::count_(0);

goby::acomms::ModemDriverBase::ModemDriverBase() : order_(++count_)
{
    // temporarily set these to the value set by the order in which the driver was started and update to more useful names in modem_start
    glog_out_group_ = "goby::acomms::modemdriver::out::" + goby::util::as<std::string>(order_);
    glog_in_group_ = "goby::acomms::modemdriver::in::" + goby::util::as<std::string>(order_);
}

goby::acomms::ModemDriverBase::~ModemDriverBase() { modem_close(); }

void goby::acomms::ModemDriverBase::modem_write(const std::string& out)
{
    if (modem_->active())
        modem_->write(out);
    else
        throw(ModemDriverException("Modem physical connection failed.",
                                   protobuf::ModemDriverStatus::CONNECTION_TO_MODEM_FAILED));
}

bool goby::acomms::ModemDriverBase::modem_read(std::string* in)
{
    if (modem_->active())
        return modem_->readline(in);
    else
        throw(ModemDriverException("Modem physical connection failed.",
                                   protobuf::ModemDriverStatus::CONNECTION_TO_MODEM_FAILED));
}

void goby::acomms::ModemDriverBase::modem_close() { modem_.reset(); }

void goby::acomms::ModemDriverBase::modem_start(const protobuf::DriverConfig& cfg)
{
    cfg_ = cfg;

    if (!cfg.has_modem_id())
        throw(ModemDriverException("missing modem_id in configuration",
                                   protobuf::ModemDriverStatus::INVALID_CONFIGURATION));

    // only set once - TODO add ability to remove groups from glog
    if (!glog_groups_set_)
    {
        glog_out_group_ = "goby::acomms::modemdriver::out::" + driver_name(cfg);
        glog_in_group_ = "goby::acomms::modemdriver::in::" + driver_name(cfg);

        goby::glog.add_group(glog_out_group_, util::Colors::lt_magenta);
        goby::glog.add_group(glog_in_group_, util::Colors::lt_blue);
        glog_groups_set_ = true;
    }

    if (cfg.has_connection_type())
    {
        switch (cfg.connection_type())
        {
            case protobuf::DriverConfig::CONNECTION_SERIAL:
                goby::glog.is(DEBUG1) && goby::glog << group(glog_out_group_)
                                                    << "opening serial port " << cfg.serial_port()
                                                    << " @ " << cfg.serial_baud() << std::endl;

                if (!cfg.has_serial_port())
                    throw(ModemDriverException("missing serial port in configuration",
                                               protobuf::ModemDriverStatus::INVALID_CONFIGURATION));
                if (!cfg.has_serial_baud())
                    throw(ModemDriverException("missing serial baud in configuration",
                                               protobuf::ModemDriverStatus::INVALID_CONFIGURATION));

                modem_.reset(new util::SerialClient(cfg.serial_port(), cfg.serial_baud(),
                                                    cfg.line_delimiter()));
                break;

            case protobuf::DriverConfig::CONNECTION_TCP_AS_CLIENT:
                goby::glog.is(DEBUG1) && goby::glog << group(glog_out_group_)
                                                    << "opening tcp client: " << cfg.tcp_server()
                                                    << ":" << cfg.tcp_port() << std::endl;
                if (!cfg.has_tcp_server())
                    throw(ModemDriverException("missing tcp server address in configuration",
                                               protobuf::ModemDriverStatus::INVALID_CONFIGURATION));
                if (!cfg.has_tcp_port())
                    throw(ModemDriverException("missing tcp port in configuration",
                                               protobuf::ModemDriverStatus::INVALID_CONFIGURATION));

                modem_.reset(new util::TCPClient(cfg.tcp_server(), cfg.tcp_port(),
                                                 cfg.line_delimiter(), cfg.reconnect_interval()));
                break;

            case protobuf::DriverConfig::CONNECTION_TCP_AS_SERVER:
                goby::glog.is(DEBUG1) && goby::glog << group(glog_out_group_)
                                                    << "opening tcp server on port"
                                                    << cfg.tcp_port() << std::endl;

                if (!cfg.has_tcp_port())
                    throw(ModemDriverException("missing tcp port in configuration",
                                               protobuf::ModemDriverStatus::INVALID_CONFIGURATION));

                modem_.reset(new util::TCPServer(cfg.tcp_port(), cfg.line_delimiter()));
        }
    }
    else {
        goby::glog.is(DEBUG1) && goby::glog << group(glog_out_group_) << warn
                                                    << "NO modem connection_type specified in your configuration file."
                                                    << std::endl;
    }

    if (cfg.has_raw_log())
    {
        using namespace boost::posix_time;
        boost::format file_format(cfg.raw_log());
        file_format.exceptions(boost::io::all_error_bits ^
                               (boost::io::too_many_args_bit | boost::io::too_few_args_bit));

        std::string file_name = (file_format % to_iso_string(second_clock::universal_time())).str();

        glog.is(DEBUG1) && glog << group(glog_out_group_)
                                << "logging raw output to file: " << file_name << std::endl;

        raw_fs_.reset(new std::ofstream(file_name.c_str()));

        if (raw_fs_->is_open())
        {
            if (!raw_fs_connections_made_)
            {
                connect(&signal_raw_incoming,
                        boost::bind(&ModemDriverBase::write_raw, this, _1, true));
                connect(&signal_raw_outgoing,
                        boost::bind(&ModemDriverBase::write_raw, this, _1, false));
                raw_fs_connections_made_ = true;
            }
        }
        else
        {
            glog.is(DEBUG1) && glog << group(glog_out_group_) << warn << "Failed to open log file"
                                    << std::endl;
            raw_fs_.reset();
        }
    }

    if (modem_)
    {
        modem_->start();

        // give it this much startup time
        const int max_startup_ms = 10000;
        int startup_elapsed_ms = 0;
        while (!modem_->active())
        {
            usleep(100000); // 100 ms
            startup_elapsed_ms += 100;
            if (startup_elapsed_ms >= max_startup_ms)
                throw(ModemDriverException("Modem physical connection failed to startup.",
                                           protobuf::ModemDriverStatus::STARTUP_FAILED));
        }
    }
    else
    {
        glog.is(DEBUG1) && glog << group(glog_out_group_) << warn 
                                            << "No modem initialized"
                                            << std::endl;
    }
}

void goby::acomms::ModemDriverBase::write_raw(const protobuf::ModemRaw& msg, bool rx)
{
    if (rx)
        *raw_fs_ << "[rx] ";
    else
        *raw_fs_ << "[tx] ";
    *raw_fs_ << msg.raw() << std::endl;
}

void goby::acomms::ModemDriverBase::update_cfg(const protobuf::DriverConfig& /*cfg*/)
{
    goby::glog.is(WARN) && goby::glog << group(glog_out_group_)
                                      << "Updating configuration is not implemented in this driver."
                                      << std::endl;
}

std::string goby::acomms::ModemDriverBase::driver_name(const protobuf::DriverConfig& cfg)
{
    const auto driver_prefix_len = strlen("DRIVER_");
    std::string driver_name = cfg.has_driver_name()
                                  ? cfg.driver_name()
                                  : goby::acomms::protobuf::DriverType_Name(cfg.driver_type())
                                        .substr(driver_prefix_len); // remove "DRIVER_"
    return driver_name + "::" + goby::util::as<std::string>(cfg.modem_id());
}

void goby::acomms::ModemDriverBase::report(protobuf::ModemReport* report)
{
    if (cfg_.has_modem_id())
        report->set_modem_id(cfg_.modem_id());
    report->set_time_with_units(goby::time::SystemClock::now<goby::time::MicroTime>());

    // default assume if we have a open serial/tcp connection that the modem is available
    // subclasses should override to provide better information
    if (modem_ && modem_->active())
        report->set_link_state(protobuf::ModemReport::LINK_AVAILABLE);
}
