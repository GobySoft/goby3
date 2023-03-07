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

#ifndef GOBY_MIDDLEWARE_COMMON_H
#define GOBY_MIDDLEWARE_COMMON_H

#include <fstream>
#include <sstream>
#include <sys/syscall.h>
#include <thread>
#include <unistd.h>

#include <boost/algorithm/string.hpp>

#include "goby/exception.h"
#include "goby/middleware/protobuf/layer.pb.h"

namespace goby
{
namespace middleware
{
inline std::string to_string(goby::middleware::protobuf::Layer layer)
{
    const int underscore_pos = 5; // underscore position in: "LAYER_"
    std::string name = goby::middleware::protobuf::Layer_Name(layer).substr(underscore_pos + 1);
    boost::to_lower(name);
    return name;
}

// unique portable thread id string from hashing std::thread::id
inline std::string thread_id(std::thread::id i = std::this_thread::get_id())
{
    std::stringstream ss;
    ss << std::hex << std::hash<std::thread::id>{}(i);
    return ss.str();
}

// Linux/Apple thread ID, useful because you can access it outside the system
#if defined __APPLE__
inline uint64_t gettid()
{
    uint64_t tid;
    pthread_threadid_np(NULL, &tid);
    return tid;
}
#elif defined SYS_gettid
inline pid_t gettid()
{
    return syscall(SYS_gettid);
}
#else
#error "SYS_gettid unavailable on this system, and this is not an Apple system."
#endif

inline std::string hostname()
{
    // read from boot id as this is static while the machine is up and running
    std::ifstream hs("/etc/hostname");
    if (hs.is_open())
    {
        std::string hostname((std::istreambuf_iterator<char>(hs)),
                             std::istreambuf_iterator<char>());
        boost::trim(hostname);
        return hostname;
    }
    else
    {
        throw(goby::Exception("Could not open /etc/hostname"));
    }
};

// hostname plus process ID
inline std::string full_process_id()
{
    static const std::string host_id = hostname();
    static const std::string pid = std::to_string(getpid());
    static const std::string full_pid = host_id + std::string("-p") + pid;
    return full_pid;
}

// full_process_id + thread_id
inline std::string full_process_and_thread_id(std::thread::id i = std::this_thread::get_id())
{
    return full_process_id() + "-t" + thread_id(i);
}
} // namespace middleware
} // namespace goby

#endif
