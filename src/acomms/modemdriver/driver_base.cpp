// Copyright 2009-2020:
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

#include <boost/format.hpp>

#include "goby/acomms/connect.h"
#include "goby/util/debug_logger.h"

#include "driver_base.h"
#include "driver_exception.h"

using namespace goby::util::logger;
using namespace goby::util::logger_lock;

std::atomic<int> goby::acomms::ModemDriverBase::count_(0);

goby::acomms::ModemDriverBase::ModemDriverBase() : raw_fs_connections_made_(false), order_(++count_)
{
    glog_out_group_ = "goby::acomms::modemdriver::out::" + goby::util::as<std::string>(order_);
    glog_in_group_ = "goby::acomms::modemdriver::in::" + goby::util::as<std::string>(order_);

    goby::glog.add_group(glog_out_group_, util::Colors::lt_magenta);
    goby::glog.add_group(glog_in_group_, util::Colors::lt_blue);
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
    if (!cfg.has_modem_id())
        throw(ModemDriverException("missing modem_id in configuration",
                                   protobuf::ModemDriverStatus::INVALID_CONFIGURATION));

    switch (cfg.connection_type())
    {
        case protobuf::DriverConfig::CONNECTION_SERIAL:
            goby::glog.is(DEBUG1) && goby::glog << group(glog_out_group_) << "opening serial port "
                                                << cfg.serial_port() << " @ " << cfg.serial_baud()
                                                << std::endl;

            if (!cfg.has_serial_port())
                throw(ModemDriverException("missing serial port in configuration",
                                           protobuf::ModemDriverStatus::INVALID_CONFIGURATION));
            if (!cfg.has_serial_baud())
                throw(ModemDriverException("missing serial baud in configuration",
                                           protobuf::ModemDriverStatus::INVALID_CONFIGURATION));

            modem_.reset(
                new util::SerialClient(cfg.serial_port(), cfg.serial_baud(), cfg.line_delimiter()));
            break;

        case protobuf::DriverConfig::CONNECTION_TCP_AS_CLIENT:
            goby::glog.is(DEBUG1) && goby::glog << group(glog_out_group_)
                                                << "opening tcp client: " << cfg.tcp_server() << ":"
                                                << cfg.tcp_port() << std::endl;
            if (!cfg.has_tcp_server())
                throw(ModemDriverException("missing tcp server address in configuration",
                                           protobuf::ModemDriverStatus::INVALID_CONFIGURATION));
            if (!cfg.has_tcp_port())
                throw(ModemDriverException("missing tcp port in configuration",
                                           protobuf::ModemDriverStatus::INVALID_CONFIGURATION));

            modem_.reset(new util::TCPClient(cfg.tcp_server(), cfg.tcp_port(), cfg.line_delimiter(),
                                             cfg.reconnect_interval()));
            break;

        case protobuf::DriverConfig::CONNECTION_TCP_AS_SERVER:
            goby::glog.is(DEBUG1) && goby::glog << group(glog_out_group_)
                                                << "opening tcp server on port" << cfg.tcp_port()
                                                << std::endl;

            if (!cfg.has_tcp_port())
                throw(ModemDriverException("missing tcp port in configuration",
                                           protobuf::ModemDriverStatus::INVALID_CONFIGURATION));

            modem_.reset(new util::TCPServer(cfg.tcp_port(), cfg.line_delimiter()));
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

void goby::acomms::ModemDriverBase::write_raw(const protobuf::ModemRaw& msg, bool rx)
{
    if (rx)
        *raw_fs_ << "[rx] ";
    else
        *raw_fs_ << "[tx] ";
    *raw_fs_ << msg.raw() << std::endl;
}

void goby::acomms::ModemDriverBase::update_cfg(const protobuf::DriverConfig& cfg)
{
    goby::glog.is(WARN) && goby::glog << group(glog_out_group_)
                                      << "Updating configuration is not implemented in this driver."
                                      << std::endl;
}
