// Copyright 2009-2018 Toby Schneider (http://gobysoft.org/index.wt/people/toby)
//                     GobySoft, LLC (2013-)
//                     Massachusetts Institute of Technology (2007-2014)
//                     Community contributors (see AUTHORS file)
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

#ifndef SerialMAVLink20190719H
#define SerialMAVLink20190719H

#include "mavlink_common.h"
#include "serial_interface.h"

namespace goby
{
namespace middleware
{
namespace io
{
template <const goby::middleware::Group& line_in_group,
          const goby::middleware::Group& line_out_group,
          PubSubLayer publish_layer = PubSubLayer::INTERPROCESS,
          PubSubLayer subscribe_layer = PubSubLayer::INTERTHREAD>
using SerialThreadMAVLinkBase =
    IOThreadMAVLink<line_in_group, line_out_group,
                    SerialThread<line_in_group, line_out_group, publish_layer, subscribe_layer>>;

/// \brief Reads/Writes MAVLink message packages from/to serial port
/// \tparam line_in_group goby::middleware::Group to publish to after receiving data from the serial port
/// \tparam line_out_group goby::middleware::Group to subcribe to for data to send to the serial port
template <const goby::middleware::Group& line_in_group,
          const goby::middleware::Group& line_out_group,
          PubSubLayer publish_layer = PubSubLayer::INTERPROCESS,
          PubSubLayer subscribe_layer = PubSubLayer::INTERTHREAD>
class SerialThreadMAVLink
    : public SerialThreadMAVLinkBase<line_in_group, line_out_group, publish_layer, subscribe_layer>
{
  public:
    SerialThreadMAVLink(const goby::middleware::protobuf::SerialConfig& config)
        : SerialThreadMAVLinkBase<line_in_group, line_out_group, publish_layer, subscribe_layer>(
              config)
    {
    }

    ~SerialThreadMAVLink() {}

  private:
    void async_read() override;
};
} // namespace io
} // namespace middleware
} // namespace goby

template <const goby::middleware::Group& line_in_group,
          const goby::middleware::Group& line_out_group,
          goby::middleware::io::PubSubLayer publish_layer,
          goby::middleware::io::PubSubLayer subscribe_layer>
void goby::middleware::io::SerialThreadMAVLink<line_in_group, line_out_group, publish_layer,
                                               subscribe_layer>::async_read()
{
    boost::asio::async_read(
        this->mutable_serial_port(), boost::asio::buffer(this->buffer()),
        boost::asio::transfer_at_least(1),
        [this](const boost::system::error_code& ec, std::size_t bytes_transferred) {
            if (!ec && bytes_transferred > 0)
            {
                this->try_parse(bytes_transferred);
                this->async_read();
            }
            else
            {
                this->handle_read_error(ec);
            }
        });
}

#endif
