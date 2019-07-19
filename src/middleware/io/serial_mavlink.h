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

#include "serial_interface.h"

#include <mavlink/v2.0/standard/standard.hpp>

namespace goby
{
namespace middleware
{
namespace io
{
/// \brief Reads/Writes MAVLink message packages from/to serial port
/// \tparam line_in_group goby::middleware::Group to publish to after receiving data from the serial port
/// \tparam line_out_group goby::middleware::Group to subcribe to for data to send to the serial port
template <const goby::middleware::Group& line_in_group,
          const goby::middleware::Group& line_out_group>
class SerialThreadMAVLink : public SerialThread<line_in_group, line_out_group>
{
    using Base = SerialThread<line_in_group, line_out_group>;

  public:
    SerialThreadMAVLink(const goby::middleware::protobuf::SerialConfig& config) : Base(config) {}

    ~SerialThreadMAVLink() {}

  private:
    void async_read() override;
    void clear_buffers()
    {
        msg_ = {};
        status_ = {};
        msg_buffer_ = {};
        status_buffer_ = {};
    }

  private:
    std::array<char, MAVLINK_MAX_PACKET_LEN> buffer_;
    mavlink::mavlink_message_t msg_{}, msg_buffer_{};
    mavlink::mavlink_status_t status_{}, status_buffer_{};
};
} // namespace io
} // namespace middleware
} // namespace goby

template <const goby::middleware::Group& line_in_group,
          const goby::middleware::Group& line_out_group>
void goby::middleware::io::SerialThreadMAVLink<line_in_group, line_out_group>::async_read()
{
    boost::asio::async_read(
        this->mutable_serial_port(), boost::asio::buffer(buffer_),
        boost::asio::transfer_at_least(1),
        [this](const boost::system::error_code& ec, std::size_t bytes_transferred) {
            if (!ec && bytes_transferred > 0)
            {
                auto bytes_begin = buffer_.begin(), bytes_end = buffer_.begin() + bytes_transferred;
                for (auto c = bytes_begin; c != bytes_end; ++c)
                {
                    try
                    {
                        auto res = mavlink::mavlink_frame_char_buffer(&msg_buffer_, &status_buffer_,
                                                                      *c, &msg_, &status_);
                        switch (res)
                        {
                            case mavlink::MAVLINK_FRAMING_OK:
                            {
                                std::array<uint8_t, MAVLINK_MAX_PACKET_LEN> buffer;
                                auto length =
                                    mavlink::mavlink_msg_to_send_buffer(&buffer[0], &msg_);
                                std::string bytes(buffer.begin(), buffer.begin() + length);
                                this->handle_read_success(length, bytes);
                                break;
                            }

                            case mavlink::MAVLINK_FRAMING_INCOMPLETE: break;

                            case mavlink::MAVLINK_FRAMING_BAD_CRC:
                                goby::glog.is_warn() && goby::glog << "BAD CRC decoding MAVLink msg"
                                                                   << std::endl;
                                clear_buffers();
                                break;
                            case mavlink::MAVLINK_FRAMING_BAD_SIGNATURE:
                                goby::glog.is_warn() &&
                                    goby::glog << "BAD SIGNATURE decoding MAVLink msg" << std::endl;
                                clear_buffers();
                                break;
                            default:
                                goby::glog.is_warn() &&
                                    goby::glog << "Unknown value " << res
                                               << " returned while decoding MAVLink msg"
                                               << std::endl;
                                clear_buffers();
                                break;
                        }
                    }
                    catch (goby::Exception& e)
                    {
                        goby::glog.is_warn() && goby::glog << "Exception decoding MAVLink msg: "
                                                           << e.what() << std::endl;
                        clear_buffers();
                    }
                }
                this->async_read();
            }
            else
            {
                this->handle_read_error(ec);
            }
        });
}

#endif
