// Copyright 2010-2020:
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

#include <unistd.h> // for sleep

#include <boost/system/error_code.hpp> // for error_code

#include "goby/exception.h" // for Exception
#include "goby/middleware/io/groups.h"
#include "goby/middleware/protobuf/io.pb.h"
#include "goby/time/convert.h"
#include "goby/time/system_clock.h"
#include "goby/util/as.h"
#include "goby/util/debug_logger/flex_ostream.h"    // for FlexOstream, glog
#include "goby/util/debug_logger/flex_ostreambuf.h" // for lock

#include "interface.h"

std::atomic<int> goby::util::LineBasedInterface::count_{0};

goby::util::LineBasedInterface::LineBasedInterface(const std::string& delimiter)
    : work_(io_), active_(false), index_{count_++}
{
    goby::glog.set_lock_action(goby::util::logger_lock::lock);

    if (delimiter.empty())
        throw Exception("Line based comms started with null string as delimiter!");

    interthread_.subscribe<groups::linebasedcomms_in>(
        [this](const goby::middleware::protobuf::IOData& data) {
            if (data.index() == index_)
            {
                glog.is_debug2() && glog << "[DATA IN]:  " << data.DebugString() << std::endl;

                in_.emplace_back();
                auto& in = in_.back();
                in.set_data(data.data());

                if (data.has_tcp_src())
                    in.set_src(data.tcp_src().addr() + ":" + std::to_string(data.tcp_src().port()));
                if (data.has_tcp_dest())
                    in.set_dest(data.tcp_dest().addr() + ":" +
                                std::to_string(data.tcp_dest().port()));

                in.set_time(goby::time::SystemClock::now<goby::time::SITime>().value());
            }
        });

    interthread_.subscribe<groups::linebasedcomms_in>(
        [this](const goby::middleware::protobuf::IOStatus& status) {
            if (status.index() == index_)
            {
                glog.is_debug2() && glog << "[STATUS]:  " << status.DebugString() << std::endl;
                if (status.state() == middleware::protobuf::IO__LINK_OPEN)
                    active_ = true;
                else
                    active_ = false;
            }
        });

    delimiter_ = delimiter;
    //    io_launcher_.reset(new IOLauncher(io_));
}

goby::util::LineBasedInterface::~LineBasedInterface() { interthread_.unsubscribe_all(); }

void goby::util::LineBasedInterface::start()
{
    if (active_)
        return;
    do_start();
}

void goby::util::LineBasedInterface::clear()
{
    interthread_.poll(std::chrono::seconds(0));
    //    std::lock_guard<std::mutex> lock(in_mutex_);
    in_.clear();
}

bool goby::util::LineBasedInterface::readline(protobuf::Datagram* msg,
                                              AccessOrder order /* = OLDEST_FIRST */)
{
    interthread_.poll(std::chrono::seconds(0));

    if (in_.empty())
    {
        return false;
    }
    else
    {
        //        std::lock_guard<std::mutex> lock(in_mutex_);
        switch (order)
        {
            case NEWEST_FIRST:
                msg->CopyFrom(in_.back());
                in_.pop_back();
                break;

            case OLDEST_FIRST:
                msg->CopyFrom(in_.front());
                in_.pop_front();
                break;
        }
        return true;
    }
}

bool goby::util::LineBasedInterface::readline(std::string* s,
                                              AccessOrder order /* = OLDEST_FIRST */)
{
    interthread_.poll(std::chrono::seconds(0));

    if (in_.empty())
    {
        return false;
    }
    else
    {
        //        std::lock_guard<std::mutex> lock(in_mutex_);
        switch (order)
        {
            case NEWEST_FIRST:
                (*s) = in_.back().data();
                in_.pop_back();
                break;

            case OLDEST_FIRST:
                (*s) = in_.front().data();
                in_.pop_front();
                break;
        }
        return true;
    }
}

void goby::util::LineBasedInterface::write(const protobuf::Datagram& msg)
{
    auto io_data = std::make_shared<goby::middleware::protobuf::IOData>();
    io_data->set_data(msg.data());
    io_data->set_index(index_);

    if (msg.has_src() && msg.has_dest())
    {
        try
        {
            middleware::protobuf::TCPEndPoint& io_src = *io_data->mutable_tcp_src();
            middleware::protobuf::TCPEndPoint& io_dest = *io_data->mutable_tcp_dest();
            const std::string& src = msg.src();
            const std::string& dest = msg.dest();

            auto src_colon_pos = src.find(':');
            auto dest_colon_pos = dest.find(':');

            if (src_colon_pos != std::string::npos)
            {
                io_src.set_addr(src.substr(0, src_colon_pos));
                io_src.set_port(goby::util::as<unsigned>(src.substr(src_colon_pos + 1)));
            }
            if (dest_colon_pos != std::string::npos)
            {
                io_dest.set_addr(dest.substr(0, dest_colon_pos));
                io_dest.set_port(goby::util::as<unsigned>(dest.substr(dest_colon_pos + 1)));
            }
        }
        catch (std::exception& e)
        {
        }
    }
    interthread_.publish<groups::linebasedcomms_out>(io_data);
    interthread_.poll(std::chrono::seconds(0));
}

void goby::util::LineBasedInterface::close() { do_close(); }
void goby::util::LineBasedInterface::sleep(int sec) { ::sleep(sec); }
