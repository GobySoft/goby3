// Copyright 2019-2020:
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

#ifndef GOBY_MIDDLEWARE_IO_LINE_BASED_PTY_H
#define GOBY_MIDDLEWARE_IO_LINE_BASED_PTY_H

#include <istream> // for istream, basic_...
#include <string>  // for string

#include <boost/asio/read_until.hpp>   // for async_read_until
#include <boost/asio/streambuf.hpp>    // for streambuf
#include <boost/system/error_code.hpp> // for error_code

#include "goby/middleware/io/detail/io_interface.h"  // for PubSubLayer
#include "goby/middleware/io/detail/pty_interface.h" // for PTYThread
#include "goby/middleware/io/line_based/common.h"    // for match_regex

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
class PTYConfig;
}
} // namespace middleware
} // namespace goby

namespace goby
{
namespace middleware
{
namespace io
{
/// \brief Reads/Writes strings from/to serial port using a line-based (typically ASCII) protocol with a defined end-of-line regex.
/// \tparam line_in_group goby::middleware::Group to publish to after receiving data from the serial port
/// \tparam line_out_group goby::middleware::Group to subcribe to for data to send to the serial port
template <const goby::middleware::Group& line_in_group,
          const goby::middleware::Group& line_out_group,
          PubSubLayer publish_layer = PubSubLayer::INTERPROCESS,
          PubSubLayer subscribe_layer = PubSubLayer::INTERTHREAD,
          template <class> class ThreadType = goby::middleware::SimpleThread>
class PTYThreadLineBased : public detail::PTYThread<line_in_group, line_out_group, publish_layer,
                                                    subscribe_layer, ThreadType>
{
    using Base = detail::PTYThread<line_in_group, line_out_group, publish_layer, subscribe_layer,
                                   ThreadType>;

  public:
    /// \brief Constructs the thread.
    /// \param config A reference to the Protocol Buffers config read by the main application at launch
    /// \param index Thread index for multiple instances in a given application (-1 indicates a single instance)
    PTYThreadLineBased(const goby::middleware::protobuf::PTYConfig& config, int index = -1)
        : Base(config, index), eol_matcher_(this->cfg().end_of_line())
    {
    }

    ~PTYThreadLineBased() {}

  private:
    /// \brief Starts an asynchronous read on the serial port until the end-of-line string is reached. When the read completes, a lambda is called that publishes the received line.
    void async_read() override;

  private:
    match_regex eol_matcher_;
    boost::asio::streambuf buffer_;
};
} // namespace io
} // namespace middleware
} // namespace goby

template <const goby::middleware::Group& line_in_group,
          const goby::middleware::Group& line_out_group,
          goby::middleware::io::PubSubLayer publish_layer,
          goby::middleware::io::PubSubLayer subscribe_layer, template <class> class ThreadType>
void goby::middleware::io::PTYThreadLineBased<line_in_group, line_out_group, publish_layer,
                                              subscribe_layer, ThreadType>::async_read()
{
    boost::asio::async_read_until(
        this->mutable_socket(), buffer_, eol_matcher_,
        [this](const boost::system::error_code& ec, std::size_t bytes_transferred) {
            if (!ec && bytes_transferred > 0)
            {
                std::string bytes(bytes_transferred, 0);
                std::istream is(&buffer_);
                is.read(&bytes[0], bytes_transferred);
                this->handle_read_success(bytes_transferred, bytes);
                this->async_read();
            }
            else
            {
                this->handle_read_error(ec);
            }
        });
}

#endif
