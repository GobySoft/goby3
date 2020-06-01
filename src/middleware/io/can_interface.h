// Copyright 2020:
//   GobySoft, LLC (2013-)
//   Community contributors (see AUTHORS file)
// File authors:
//   Shawn Dooley <shawn@shawndooley.net>
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

#ifndef CanInterface20200422H
#define CanInterface20200422H

#include <boost/asio/posix/stream_descriptor.hpp>
#include <boost/asio/streambuf.hpp>
#include <boost/asio/write.hpp>
#include <boost/units/systems/si/prefixes.hpp>

#include <linux/can.h>
#include <linux/can/raw.h>

#include "goby/middleware/io/common.h"
#include "goby/middleware/protobuf/can_config.pb.h"

namespace goby
{
namespace middleware
{
namespace io
{
template <const goby::middleware::Group& line_in_group,
          const goby::middleware::Group& line_out_group,
          // by default publish all incoming traffic to interprocess for logging
          PubSubLayer publish_layer = PubSubLayer::INTERPROCESS,
          // but only subscribe on interthread for outgoing traffic
          PubSubLayer subscribe_layer = PubSubLayer::INTERTHREAD>
class CanThread
    : public IOThread<line_in_group, line_out_group, publish_layer, subscribe_layer,
                      goby::middleware::protobuf::CanConfig, boost::asio::posix::stream_descriptor>
{
    using Base =
        IOThread<line_in_group, line_out_group, publish_layer, subscribe_layer,
                 goby::middleware::protobuf::CanConfig, boost::asio::posix::stream_descriptor>;

  public:
    /// \brief Constructs the thread.
    /// \param config A reference to the Protocol Buffers config read by the main application at launch
    /// \param index Thread index for multiple instances in a given application (-1 indicates a single instance)
    CanThread(const goby::middleware::protobuf::CanConfig& config, int index = -1)
        : Base(config, index, std::string("can: ") + config.interface())
    {
    }

    ~CanThread() {}

  protected:
    virtual void async_write(const std::string& bytes) override;

  private:
    void async_read() override;
    void open_socket() override;

    void data_rec(struct can_frame& receive_frame_, boost::asio::posix::stream_descriptor& stream);

  private:
    struct can_frame receive_frame_;
};
} // namespace io
} // namespace middleware
} // namespace goby

template <const goby::middleware::Group& line_in_group,
          const goby::middleware::Group& line_out_group,
          goby::middleware::io::PubSubLayer publish_layer,
          goby::middleware::io::PubSubLayer subscribe_layer>
void goby::middleware::io::CanThread<line_in_group, line_out_group, publish_layer,
                                     subscribe_layer>::open_socket()
{
    int can_socket;

    struct sockaddr_can addr_;
    struct can_frame receive_frame_;
    struct ifreq ifr_;
    can_socket = socket(PF_CAN, SOCK_RAW, CAN_RAW);

    std::vector<struct can_filter> filters;

    for (auto x : this->cfg().filter()) { filters.push_back({x.can_id(), x.can_mask()}); }

    if (filters.size())
    {
        setsockopt(can_socket, SOL_CAN_RAW, CAN_RAW_FILTER, filters.data(),
                   sizeof(can_filter) * filters.size());
    }
    std::strcpy(ifr_.ifr_name, this->cfg().interface().c_str());

    ioctl(can_socket, SIOCGIFINDEX, &ifr_);

    addr_.can_family = AF_CAN;
    addr_.can_ifindex = ifr_.ifr_ifindex;
    if (bind(can_socket, (struct sockaddr*)&addr_, sizeof(addr_)) < 0)
    {
        glog.is_die() && glog << "Error in socket bind to interface " << this->cfg().interface()
                              << ": " << std::strerror(errno) << std::endl;
    }

    this->mutable_socket().assign(can_socket);
}

template <const goby::middleware::Group& line_in_group,
          const goby::middleware::Group& line_out_group,
          goby::middleware::io::PubSubLayer publish_layer,
          goby::middleware::io::PubSubLayer subscribe_layer>
void goby::middleware::io::CanThread<line_in_group, line_out_group, publish_layer,
                                     subscribe_layer>::async_write(const std::string& bytes)
{
    // TODO: Check frame validity?

    this->mutable_socket().async_write_some(
        boost::asio::buffer(bytes),
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

template <const goby::middleware::Group& line_in_group,
          const goby::middleware::Group& line_out_group,
          goby::middleware::io::PubSubLayer publish_layer,
          goby::middleware::io::PubSubLayer subscribe_layer>
void goby::middleware::io::CanThread<line_in_group, line_out_group, publish_layer,
                                     subscribe_layer>::async_read()
{
    boost::asio::async_read(this->mutable_socket(),
                            boost::asio::buffer(&receive_frame_, sizeof(receive_frame_)),
                            boost::bind(&CanThread::data_rec, this, boost::ref(receive_frame_),
                                        boost::ref(this->mutable_socket())));
}

template <const goby::middleware::Group& line_in_group,
          const goby::middleware::Group& line_out_group,
          goby::middleware::io::PubSubLayer publish_layer,
          goby::middleware::io::PubSubLayer subscribe_layer>
void goby::middleware::io::
    CanThread<line_in_group, line_out_group, publish_layer, subscribe_layer>::data_rec(
        struct can_frame& receive_frame_, boost::asio::posix::stream_descriptor& stream)
{
    //  Within a process raw can frames are probably what we are looking for.
    this->interthread().template publish<line_in_group>(receive_frame_);

    std::string bytes;
    const int frame_size = sizeof(can_frame);

    for (int i = 0; i < frame_size; ++i)
    { bytes += *(reinterpret_cast<char*>(&receive_frame_) + i); }

    this->handle_read_success(bytes.size(), bytes);

    boost::asio::async_read(
        stream, boost::asio::buffer(&receive_frame_, sizeof(receive_frame_)),
        boost::bind(&CanThread::data_rec, this, boost::ref(receive_frame_), boost::ref(stream)));
}

#endif
