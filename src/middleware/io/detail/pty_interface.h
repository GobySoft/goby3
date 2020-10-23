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

#pragma once

#include <boost/asio/posix/stream_descriptor.hpp>
#include <boost/asio/streambuf.hpp>
#include <boost/asio/write.hpp>
#include <boost/units/systems/si/prefixes.hpp>

#include "goby/middleware/io/detail/io_interface.h"
#include "goby/middleware/protobuf/pty_config.pb.h"

#include <sys/stat.h>

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
          PubSubLayer subscribe_layer = PubSubLayer::INTERTHREAD>
class PTYThread : public detail::IOThread<line_in_group, line_out_group, publish_layer,
                                          subscribe_layer, goby::middleware::protobuf::PTYConfig,
                                          boost::asio::posix::stream_descriptor>
{
    using Base = detail::IOThread<line_in_group, line_out_group, publish_layer, subscribe_layer,
                                  goby::middleware::protobuf::PTYConfig,
                                  boost::asio::posix::stream_descriptor>;

  public:
    /// \brief Constructs the thread.
    /// \param config A reference to the Protocol Buffers config read by the main application at launch
    /// \param index Thread index for multiple instances in a given application (-1 indicates a single instance)
    PTYThread(const goby::middleware::protobuf::PTYConfig& config, int index = -1)
        : Base(config, index, std::string("pty: ") + config.name())
    {
    }

    ~PTYThread() {}

  protected:
    virtual void async_write(const std::string& bytes) override;

  private:
    void open_socket() override;
};
} // namespace detail
} // namespace io
} // namespace middleware
} // namespace goby

template <const goby::middleware::Group& line_in_group,
          const goby::middleware::Group& line_out_group,
          goby::middleware::io::PubSubLayer publish_layer,
          goby::middleware::io::PubSubLayer subscribe_layer>
void goby::middleware::io::detail::PTYThread<line_in_group, line_out_group, publish_layer,
                                             subscribe_layer>::open_socket()
{
    int pty_internal = posix_openpt(O_RDWR | O_NOCTTY);

    if (pty_internal == -1)
        throw(goby::Exception(std::string("Error in posix_openpt: ") + std::strerror(errno)));
    if (grantpt(pty_internal) == -1)
        throw(goby::Exception(std::string("Error in grantpt: ") + std::strerror(errno)));
    if (unlockpt(pty_internal) == -1)
        throw(goby::Exception(std::string("Error in unlockpt: ") + std::strerror(errno)));

    char pty_external_path[256];
    ptsname_r(pty_internal, pty_external_path, sizeof(pty_external_path));
    const char* pty_external_symlink = this->cfg().name().c_str();

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

    if (symlink(pty_external_path, pty_external_symlink) == -1)
        throw(goby::Exception(std::string("Could not create symlink: ") + pty_external_symlink));

    this->mutable_socket().assign(pty_internal);
    this->mutable_socket().non_blocking(true);
}

template <const goby::middleware::Group& line_in_group,
          const goby::middleware::Group& line_out_group,
          goby::middleware::io::PubSubLayer publish_layer,
          goby::middleware::io::PubSubLayer subscribe_layer>
void goby::middleware::io::detail::PTYThread<line_in_group, line_out_group, publish_layer,
                                             subscribe_layer>::async_write(const std::string& bytes)
{
    boost::asio::async_write(
        this->mutable_socket(), boost::asio::buffer(bytes),
        [this](const boost::system::error_code& ec, std::size_t bytes_transferred) {
            if (!ec && bytes_transferred > 0)
            {
                this->handle_write_success(bytes_transferred);
            }
            else
            {
                this->handle_write_error(ec);
            }
        });
}
