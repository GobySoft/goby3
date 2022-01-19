// Copyright :
//   Community contributors (see AUTHORS file)
// File authors:
//   Not Committed Yet
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

#ifndef GOBY_MIDDLEWARE_IO_COBS_TCP_CLIENT_H
#define GOBY_MIDDLEWARE_IO_COBS_TCP_CLIENT_H

#include <istream> // for istream
#include <memory>  // for make_shared
#include <string>  // for basic_st...

#include <boost/asio/read_until.hpp>   // for async_re...
#include <boost/asio/streambuf.hpp>    // for streambuf
#include <boost/system/error_code.hpp> // for error_code

#include "goby/middleware/io/cobs/common.h"
#include "goby/middleware/io/detail/io_interface.h"         // for PubSubLayer
#include "goby/middleware/io/detail/tcp_client_interface.h" // for TCPClien...
#include "goby/middleware/protobuf/io.pb.h"                 // for IOData

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
class TCPClientConfig;
}
} // namespace middleware
} // namespace goby

namespace goby
{
namespace middleware
{
namespace io
{
/// \brief Reads/Writes strings from/to a TCP connection using a Consistent Overhead Byte Stuffing (COBS) binary protocol.
/// \tparam packet_in_group goby::middleware::Group to publish to after receiving data from the TCP socket
/// \tparam packet_out_group goby::middleware::Group to subcribe to for data to send to the TCP socket
template <const goby::middleware::Group& packet_in_group,
          const goby::middleware::Group& packet_out_group,
          PubSubLayer publish_layer = PubSubLayer::INTERPROCESS,
          PubSubLayer subscribe_layer = PubSubLayer::INTERTHREAD,
          typename Config = goby::middleware::protobuf::TCPClientConfig,
          template <class> class ThreadType = goby::middleware::SimpleThread,
          bool use_indexed_groups = false>
class TCPClientThreadCOBS
    : public detail::TCPClientThread<packet_in_group, packet_out_group, publish_layer,
                                     subscribe_layer, Config, ThreadType, use_indexed_groups>
{
    using Base = detail::TCPClientThread<packet_in_group, packet_out_group, publish_layer,
                                         subscribe_layer, Config, ThreadType, use_indexed_groups>;

  public:
    /// \brief Constructs the thread.
    /// \param config A reference to the Protocol Buffers config read by the main application at launch
    /// \param index Thread index for multiple instances in a given application (-1 indicates a single instance)
    TCPClientThreadCOBS(const goby::middleware::protobuf::TCPClientConfig& config, int index = -1)
        : Base(config, index)
    {
    }

    ~TCPClientThreadCOBS() {}

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
