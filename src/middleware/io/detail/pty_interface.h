// Copyright 2020:
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

#ifndef GOBY_MIDDLEWARE_IO_DETAIL_PTY_INTERFACE_H
#define GOBY_MIDDLEWARE_IO_DETAIL_PTY_INTERFACE_H

#include <errno.h>    // for errno
#include <fcntl.h>    // for O_NOCTTY, O_RDWR
#include <memory>     // for shared_ptr
#include <stdio.h>    // for remove
#include <stdlib.h>   // for grantpt, posix_o...
#include <string.h>   // for strerror
#include <string>     // for operator+, string
#include <sys/stat.h> // for lstat, stat, S_I...
#include <termios.h>  // for cfsetspeed, cfma...
#include <unistd.h>   // for symlink

#include <boost/asio/posix/stream_descriptor.hpp> // for stream_descriptor

#include "goby/exception.h"                         // for Exception
#include "goby/middleware/io/detail/io_interface.h" // for PubSubLayer, IOT...
#include "goby/middleware/protobuf/pty_config.pb.h" // for PTYConfig

namespace goby
{
namespace middleware
{
class Group;
}
} // namespace goby
namespace goby
{
namespace middleware
{
namespace protobuf
{
class IOData;
}
} // namespace middleware
} // namespace goby

namespace goby
{
namespace middleware
{
namespace io
{
namespace detail
{
template <const goby::middleware::Group& line_in_group,
          const goby::middleware::Group& line_out_group,
          // by default publish all incoming traffic to interprocess for logging
          PubSubLayer publish_layer = PubSubLayer::INTERPROCESS,
          // but only subscribe on interthread for outgoing traffic
          PubSubLayer subscribe_layer = PubSubLayer::INTERTHREAD,
          template <class> class ThreadType = goby::middleware::SimpleThread>
class PTYThread : public detail::IOThread<line_in_group, line_out_group, publish_layer,
                                          subscribe_layer, goby::middleware::protobuf::PTYConfig,
                                          boost::asio::posix::stream_descriptor, ThreadType>
{
    using Base = detail::IOThread<line_in_group, line_out_group, publish_layer, subscribe_layer,
                                  goby::middleware::protobuf::PTYConfig,
                                  boost::asio::posix::stream_descriptor, ThreadType>;

  public:
    /// \brief Constructs the thread.
    /// \param config A reference to the Protocol Buffers config read by the main application at launch
    /// \param index Thread index for multiple instances in a given application (-1 indicates a single instance)
    PTYThread(const goby::middleware::protobuf::PTYConfig& config, int index = -1)
        : Base(config, index, std::string("pty: ") + config.port())
    {
        auto ready = ThreadState::SUBSCRIPTIONS_COMPLETE;
        this->interthread().template publish<line_in_group>(ready);
    }

    ~PTYThread() override {}

  private:
    void async_write(std::shared_ptr<const goby::middleware::protobuf::IOData> io_msg) override
    {
        basic_async_write(this, io_msg);
    }

    void open_socket() override;
};
} // namespace detail
} // namespace io
} // namespace middleware
} // namespace goby

template <const goby::middleware::Group& line_in_group,
          const goby::middleware::Group& line_out_group,
          goby::middleware::io::PubSubLayer publish_layer,
          goby::middleware::io::PubSubLayer subscribe_layer, template <class> class ThreadType>
void goby::middleware::io::detail::PTYThread<line_in_group, line_out_group, publish_layer,
                                             subscribe_layer, ThreadType>::open_socket()
{
    // remove old symlink
    const char* pty_external_symlink = this->cfg().port().c_str();
    struct stat stat_buffer;
    // file exists
    if (lstat(pty_external_symlink, &stat_buffer) == 0)
    {
        if (S_ISLNK(stat_buffer.st_mode) == 1)
        {
            if (remove(pty_external_symlink) == -1)
                throw(goby::Exception(std::string("Could not remove existing symlink: ") +
                                      pty_external_symlink));
        }
        else
        {
            throw(goby::Exception(std::string("File exists and is not symlink: ") +
                                  pty_external_symlink));
        }
    }

    // open the PTY
    int pty_internal = posix_openpt(O_RDWR | O_NOCTTY);

    if (pty_internal == -1)
        throw(goby::Exception(std::string("Error in posix_openpt: ") + std::strerror(errno)));
    if (grantpt(pty_internal) == -1)
        throw(goby::Exception(std::string("Error in grantpt: ") + std::strerror(errno)));
    if (unlockpt(pty_internal) == -1)
        throw(goby::Exception(std::string("Error in unlockpt: ") + std::strerror(errno)));

    // structure to store the port settings in
    termios ps;
    if (tcgetattr(pty_internal, &ps) == -1)
        throw(goby::Exception(std::string("Unable to get attributes for pty configuration: ") +
                              strerror(errno)));

    // raw mode
    // https://man7.org/linux/man-pages/man3/cfmakeraw.3.html
    // ps.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP
    //                        | INLCR | IGNCR | ICRNL | IXON);
    // ps.c_oflag &= ~OPOST;
    // ps.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
    // ps.c_cflag &= ~(CSIZE | PARENB);
    // ps.c_cflag |= CS8;
    cfmakeraw(&ps);

    switch (this->cfg().baud())
    {
        case 2400: cfsetspeed(&ps, B2400); break;
        case 4800: cfsetspeed(&ps, B4800); break;
        case 9600: cfsetspeed(&ps, B9600); break;
        case 19200: cfsetspeed(&ps, B19200); break;
        case 38400: cfsetspeed(&ps, B38400); break;
        case 57600: cfsetspeed(&ps, B57600); break;
        case 115200: cfsetspeed(&ps, B115200); break;
        default:
            throw(goby::Exception(std::string("Invalid baud rate: ") +
                                  std::to_string(this->cfg().baud())));
    }

    // set no parity, stop bits, data bits
    ps.c_cflag &= ~CSTOPB;
    // no flow control
    ps.c_cflag &= ~CRTSCTS;

    if (tcsetattr(pty_internal, TCSANOW, &ps) == -1)
        throw(goby::Exception(std::string("Unable to set pty configuration attributes ") +
                              strerror(errno)));

    this->mutable_socket().assign(pty_internal);

    // re-symlink to new PTY
    char pty_external_path[256];
    ptsname_r(pty_internal, pty_external_path, sizeof(pty_external_path));

    if (symlink(pty_external_path, pty_external_symlink) == -1)
        throw(goby::Exception(std::string("Could not create symlink: ") + pty_external_symlink));
}

#endif
