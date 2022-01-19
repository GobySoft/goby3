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

#ifndef GOBY_MIDDLEWARE_IO_COBS_COMMON_H
#define GOBY_MIDDLEWARE_IO_COBS_COMMON_H

#include <memory>

#include <boost/asio/read.hpp>  // for async_read
#include <boost/asio/write.hpp> // for async_write

#include "goby/middleware/protobuf/io.pb.h"
#include "goby/util/binary.h"
#include "goby/util/debug_logger.h" // for glog
#include "goby/util/thirdparty/cobs/cobs.h"

namespace goby
{
namespace middleware
{
namespace io
{
template <class Thread>
void cobs_async_write(Thread* this_thread,
                      std::shared_ptr<const goby::middleware::protobuf::IOData> io_msg)
{
    constexpr static char cobs_eol{0};

    // COBS worst case is 1 byte for every 254 bytes of input
    auto input_size = io_msg->data().size();
    auto output_size_max = input_size + (input_size / 254) + 1;
    std::string cobs_encoded(output_size_max, '\0');

    auto cobs_size = cobs_encode(reinterpret_cast<const uint8_t*>(io_msg->data().data()),
                                 input_size, reinterpret_cast<uint8_t*>(&cobs_encoded[0]));

    if (cobs_size)
    {
        cobs_encoded.resize(cobs_size);
        cobs_encoded += cobs_eol;

        boost::asio::async_write(this_thread->mutable_socket(), boost::asio::buffer(cobs_encoded),
                                 [this_thread, cobs_encoded](const boost::system::error_code& ec,
                                                             std::size_t bytes_transferred) {
                                     if (!ec && bytes_transferred > 0)
                                     {
                                         this_thread->handle_write_success(bytes_transferred);
                                     }
                                     else
                                     {
                                         this_thread->handle_write_error(ec);
                                     }
                                 });
    }
    else
    {
        goby::glog.is_warn() && goby::glog << group(this_thread->glog_group())
                                           << "Failed to encode COBS message: "
                                           << goby::util::hex_encode(io_msg->data()) << std::endl;
        this_thread->handle_write_error(boost::system::error_code());
    }
}

template <class Thread, class ThreadBase = Thread>
void cobs_async_read(Thread* this_thread,
                     std::shared_ptr<ThreadBase> self = std::shared_ptr<ThreadBase>())
{
    constexpr static char cobs_eol{0};

    boost::asio::async_read_until(
        this_thread->mutable_socket(), this_thread->buffer_, cobs_eol,
        [this_thread, self](const boost::system::error_code& ec, std::size_t bytes_transferred) {
            if (!ec && bytes_transferred > 0)
            {
                std::string bytes(bytes_transferred, '\0');
                std::istream is(&this_thread->buffer_);
                is.read(&bytes[0], bytes_transferred);

                auto io_msg = std::make_shared<goby::middleware::protobuf::IOData>();
                auto& cobs_decoded = *io_msg->mutable_data();
                cobs_decoded = std::string(bytes_transferred, '\0');
                int decoded_size =
                    cobs_decode(reinterpret_cast<const uint8_t*>(bytes.data()), bytes_transferred,
                                reinterpret_cast<uint8_t*>(&cobs_decoded[0]));
                if (decoded_size)
                {
                    // decoded size includes final 0 so remove last byte?
                    cobs_decoded.resize(decoded_size - 1);
                    this_thread->handle_read_success(bytes_transferred, io_msg);
                    this_thread->async_read();
                }
                else
                {
                    goby::glog.is_warn() && goby::glog << group(this_thread->glog_group())
                                                       << "Failed to decode COBS message: "
                                                       << goby::util::hex_encode(bytes)
                                                       << std::endl;
                    this_thread->handle_read_error(ec);
                }
            }
            else
            {
                this_thread->handle_read_error(ec);
            }
        });
}

} // namespace io
} // namespace middleware
} // namespace goby

#endif
