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

#include "goby/exception.h"                         // for Exception
#include "goby/util/debug_logger/flex_ostream.h"    // for FlexOstream, glog
#include "goby/util/debug_logger/flex_ostreambuf.h" // for lock

#include "interface.h"

goby::util::LineBasedInterface::LineBasedInterface(const std::string& delimiter)
    : work_(io_), active_(false)
{
    goby::glog.set_lock_action(goby::util::logger_lock::lock);

    if (delimiter.empty())
        throw Exception("Line based comms started with null string as delimiter!");

    delimiter_ = delimiter;
    io_launcher_.reset(new IOLauncher(io_));
}

void goby::util::LineBasedInterface::start()
{
    if (active_)
        return;

    //    active_ = true;
    io_.post(boost::bind(&LineBasedInterface::do_start, this));
}

void goby::util::LineBasedInterface::clear()
{
    std::lock_guard<std::mutex> lock(in_mutex_);
    in_.clear();
}

bool goby::util::LineBasedInterface::readline(protobuf::Datagram* msg,
                                              AccessOrder order /* = OLDEST_FIRST */)
{
    if (in_.empty())
    {
        return false;
    }
    else
    {
        std::lock_guard<std::mutex> lock(in_mutex_);
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
    if (in_.empty())
    {
        return false;
    }
    else
    {
        std::lock_guard<std::mutex> lock(in_mutex_);
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

// pass the write data via the io service in the other thread
void goby::util::LineBasedInterface::write(const protobuf::Datagram& msg)
{
    io_.post(boost::bind(&LineBasedInterface::do_write, this, msg));
}

// call the do_close function via the io service in the other thread
void goby::util::LineBasedInterface::close()
{
    io_.post(boost::bind(&LineBasedInterface::do_close, this, boost::system::error_code()));
}

void goby::util::LineBasedInterface::sleep(int sec) { io_.post(boost::bind(::sleep, sec)); }
