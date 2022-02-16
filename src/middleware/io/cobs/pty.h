// Copyright 2019-2022:
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

#ifndef GOBY_MIDDLEWARE_IO_COBS_PTY_H
#define GOBY_MIDDLEWARE_IO_COBS_PTY_H

#include <istream> // for istream, basic_...
#include <string>  // for string

#include <boost/asio/read_until.hpp>   // for async_read_until
#include <boost/asio/streambuf.hpp>    // for streambuf
#include <boost/system/error_code.hpp> // for error_code

#include "goby/middleware/io/cobs/common.h"
#include "goby/middleware/io/detail/io_interface.h"  // for PubSubLayer
#include "goby/middleware/io/detail/pty_interface.h" // for PTYThread

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
class PTYThreadCOBS : public detail::PTYThread<line_in_group, line_out_group, publish_layer,
                                               subscribe_layer, ThreadType>
{
    using Base = detail::PTYThread<line_in_group, line_out_group, publish_layer, subscribe_layer,
                                   ThreadType>;

  public:
    /// \brief Constructs the thread.
    /// \param config A reference to the Protocol Buffers config read by the main application at launch
    /// \param index Thread index for multiple instances in a given application (-1 indicates a single instance)
    PTYThreadCOBS(const goby::middleware::protobuf::PTYConfig& config, int index = -1)
        : Base(config, index)
    {
    }

    ~PTYThreadCOBS() {}

    template <class Thread>
    friend void cobs_async_write(Thread* this_thread,
                                 std::shared_ptr<const goby::middleware::protobuf::IOData> io_msg);

    template <class Thread, class ThreadBase>
    friend void cobs_async_read(Thread* this_thread, std::shared_ptr<ThreadBase> self);

  private:
    void async_read() override { cobs_async_read(this); }

    void async_write(std::shared_ptr<const goby::middleware::protobuf::IOData> io_msg) override
    {
        cobs_async_write(this, io_msg);
    }

  private:
    boost::asio::streambuf buffer_;
};
} // namespace io
} // namespace middleware
} // namespace goby

#endif
