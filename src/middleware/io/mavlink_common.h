// Copyright 2019:
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

#ifndef MAVLinkCommon20190815H
#define MAVLinkCommon20190815H

#include <mavlink/v2.0/common/common.hpp>

#include "goby/middleware/group.h"
#include "goby/middleware/marshalling/mavlink.h"
#include "goby/middleware/protobuf/io.pb.h"

#include "common.h"

namespace goby
{
namespace middleware
{
namespace io
{
template <const goby::middleware::Group& line_in_group,
          const goby::middleware::Group& line_out_group, PubSubLayer publish_layer,
          PubSubLayer subscribe_layer, typename IOThreadBase, typename IOConfig>
class IOThreadMAVLink : public IOThreadBase
{
  public:
    IOThreadMAVLink(const IOConfig& config) : IOThreadBase(config)
    {
        // subscribe directly to mavlink_message_t as well
        if (subscribe_layer == PubSubLayer::INTERPROCESS)
        {
            auto msg_out_callback = [this](std::shared_ptr<const mavlink::mavlink_message_t> msg,
                                           const std::string& type) {
                goby::glog.is_debug2() &&
                    goby::glog << "writing msg [sysid: " << static_cast<int>(msg->sysid)
                               << ", compid: " << static_cast<int>(msg->compid)
                               << "] of msgid: " << static_cast<int>(msg->msgid) << std::endl;
                auto io_msg = std::make_shared<goby::middleware::protobuf::IOData>();
                auto data = SerializerParserHelper<mavlink::mavlink_message_t,
                                                   MarshallingScheme::MAVLINK>::serialize(*msg);
                io_msg->set_data(&data[0], data.size());
                this->write(io_msg);
            };

            this->interprocess()
                .template subscribe_type_regex<line_out_group, mavlink::mavlink_message_t>(
                    msg_out_callback);
        }
    }

    ~IOThreadMAVLink() {}

  protected:
    void try_parse(std::size_t bytes_transferred);
    std::array<char, MAVLINK_MAX_PACKET_LEN>& buffer() { return buffer_; }

  private:
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

template <
    const goby::middleware::Group& line_in_group, const goby::middleware::Group& line_out_group,
    goby::middleware::io::PubSubLayer publish_layer,
    goby::middleware::io::PubSubLayer subscribe_layer, typename IOThreadBase, typename IOConfig>
void goby::middleware::io::IOThreadMAVLink<line_in_group, line_out_group, publish_layer,
                                           subscribe_layer, IOThreadBase,
                                           IOConfig>::try_parse(std::size_t bytes_transferred)
{
    auto bytes_begin = buffer_.begin(), bytes_end = buffer_.begin() + bytes_transferred;
    for (auto c = bytes_begin; c != bytes_end; ++c)
    {
        try
        {
            auto res = mavlink::mavlink_frame_char_buffer(&msg_buffer_, &status_buffer_, *c, &msg_,
                                                          &status_);
            switch (res)
            {
                case mavlink::MAVLINK_FRAMING_BAD_CRC:
                    if (status_.parse_state != mavlink::MAVLINK_PARSE_STATE_IDLE)
                    {
                        break; // keep parsing
                    }
                    else if (mavlink::mavlink_get_msg_entry(msg_.msgid) == nullptr)
                    {
                        goby::glog.is_debug3() &&
                            goby::glog << "BAD CRC decoding MAVLink msg, but "
                                          "forwarding because we don't know this msgid"
                                       << std::endl;
                        // forward anyway as it might be a msgid we don't know
                        // so flow through here (no break) is intentional!!
                    }
                    else
                    {
                        goby::glog.is_warn() && goby::glog << "BAD CRC decoding MAVLink msg"
                                                           << std::endl;
                        break;
                    }

                case mavlink::MAVLINK_FRAMING_OK:
                {
                    goby::glog.is_debug3() && goby::glog << "Parsed message of id: " << msg_.msgid
                                                         << std::endl;

                    this->publish_transporter().template publish<line_in_group>(msg_);

                    std::array<uint8_t, MAVLINK_MAX_PACKET_LEN> buffer;
                    auto length = mavlink::mavlink_msg_to_send_buffer(&buffer[0], &msg_);
                    std::string bytes(buffer.begin(), buffer.begin() + length);
                    this->handle_read_success(length, bytes);
                    break;
                }

                case mavlink::MAVLINK_FRAMING_INCOMPLETE: break;

                case mavlink::MAVLINK_FRAMING_BAD_SIGNATURE:
                    goby::glog.is_warn() && goby::glog << "BAD SIGNATURE decoding MAVLink msg"
                                                       << std::endl;
                    break;
                default:
                    goby::glog.is_warn() && goby::glog << "Unknown value " << res
                                                       << " returned while decoding MAVLink msg"
                                                       << std::endl;
                    break;
            }
        }
        catch (goby::Exception& e)
        {
            goby::glog.is_warn() && goby::glog << "Exception decoding MAVLink msg: " << e.what()
                                               << std::endl;
            clear_buffers();
        }
    }
}

#endif
