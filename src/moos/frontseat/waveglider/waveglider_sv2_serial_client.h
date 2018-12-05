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

#include <boost/asio.hpp>
#include <dccl/binary.h>

namespace goby
{
namespace moos
{
class SV2SerialConnection : public boost::enable_shared_from_this<SV2SerialConnection>
{
  public:
    static boost::shared_ptr<SV2SerialConnection> create(boost::asio::io_service& io_service,
                                                         std::string name, int baud = 115200)
    {
        return boost::shared_ptr<SV2SerialConnection>(
            new SV2SerialConnection(io_service, name, baud));
    }

    boost::asio::serial_port& socket() { return socket_; }

    void start() { read_start(); }

    void close() { socket_.close(); }

    void read_start()
    {
        std::fill(message_.begin(), message_.end(), 0);
        last_escape_ = 0;

        boost::asio::async_read(
            socket_, buffer_, boost::asio::transfer_exactly(1),
            boost::bind(&SV2SerialConnection::handle_read, this, _1, _2, PART_MAGIC));
    }

    void write_start(std::string data)
    {
        add_escapes(&data);
        boost::asio::async_write(socket_, boost::asio::buffer(data),
                                 boost::bind(&SV2SerialConnection::handle_write, this, _1, _2));
    }

    ~SV2SerialConnection() {}

    boost::signals2::signal<void(const std::string& message)> message_signal;

  private:
    enum MessagePart
    {
        PART_MAGIC,
        PART_HEADER,
        PART_COMPLETE
    };

    SV2SerialConnection(boost::asio::io_service& io_service, std::string name, int baud)
        : socket_(io_service),
          message_(SV2_MAX_SIZE * 2) // leave room for escape chars (in theory, every byte)
    {
        using goby::glog;
        using goby::common::logger::DIE;
        try
        {
            socket_.open(name);
        }
        catch (std::exception& e)
        {
            glog.is(DIE) && glog << "Failed to open serial port: " << name << std::endl;
        }

        socket_.set_option(boost::asio::serial_port_base::baud_rate(baud));

        // no flow control
        socket_.set_option(boost::asio::serial_port_base::flow_control(
            boost::asio::serial_port_base::flow_control::none));

        // 8N1
        socket_.set_option(boost::asio::serial_port_base::character_size(8));
        socket_.set_option(
            boost::asio::serial_port_base::parity(boost::asio::serial_port_base::parity::none));
        socket_.set_option(boost::asio::serial_port_base::stop_bits(
            boost::asio::serial_port_base::stop_bits::one));
    }

    void handle_write(const boost::system::error_code& error, size_t bytes_transferred)
    {
        if (error)
        {
            using goby::glog;
            using goby::common::logger::WARN;
            glog.is(WARN) && glog << "Error writing to serial connection: " << error << std::endl;
        }
    }

    void handle_read(const boost::system::error_code& error, size_t bytes_transferred,
                     MessagePart part)
    {
        using goby::glog;
        using goby::common::logger::DEBUG1;
        using goby::common::logger::DEBUG2;
        using goby::common::logger::WARN;

        if (!error)
        {
            std::istream istrm(&buffer_);

            switch (part)
            {
                case PART_MAGIC:
                {
                    if ((istrm.peek() & 0xFF) != SV2_MAGIC_BYTE)
                    {
                        glog.is(DEBUG2) && glog << std::hex << istrm.peek() << std::dec
                                                << ": Not magic byte, continuing to read."
                                                << std::endl;
                        read_start();
                        buffer_.consume(bytes_transferred);
                    }
                    else
                    {
                        glog.is(DEBUG2) && glog << "Received magic byte. Starting read of "
                                                << SV2_HEADER_SIZE << " bytes" << std::endl;
                        message_insert_ = &message_[0];
                        istrm.read(message_insert_, bytes_transferred);
                        message_insert_ += bytes_transferred;

                        // read the header
                        boost::asio::async_read(socket_, buffer_,
                                                boost::asio::transfer_exactly(SV2_HEADER_SIZE),
                                                boost::bind(&SV2SerialConnection::handle_read, this,
                                                            _1, _2, PART_HEADER));
                    }
                    break;
                }

                case PART_HEADER:
                {
                    istrm.read(message_insert_, bytes_transferred);
                    message_insert_ += bytes_transferred;

                    if (!check_escapes(part))
                        return;
                    remove_escapes();

                    message_size_ = message_[1] & 0xFF;
                    message_size_ |= (message_[2] & 0xFF) << 8;

                    glog.is(DEBUG2) &&
                        glog << "Received header: "
                             << dccl::hex_encode(std::string(
                                    message_.begin(), message_.begin() + SV2_HEADER_SIZE + 1))
                             << ", size of " << message_size_ << std::endl;

                    // read the rest of the message
                    boost::asio::async_read(
                        socket_, buffer_,
                        boost::asio::transfer_exactly(message_size_ - SV2_HEADER_SIZE),
                        boost::bind(&SV2SerialConnection::handle_read, this, _1, _2,
                                    PART_COMPLETE));
                    break;
                }

                case PART_COMPLETE:
                {
                    istrm.read(message_insert_, bytes_transferred);
                    message_insert_ += bytes_transferred;

                    if (!check_escapes(part))
                        return;
                    remove_escapes();

                    glog.is(DEBUG1) &&
                        glog << "Read message: "
                             << dccl::hex_encode(std::string(message_.begin(),
                                                             message_.begin() + message_size_ + 1))
                             << std::endl;
                    message_signal(
                        std::string(message_.begin(), message_.begin() + message_size_ + 1));
                    read_start();

                    break;
                }
            }
        }
        else
        {
            if (error == boost::asio::error::eof)
            {
                glog.is(DEBUG1) && glog << "Connection reached EOF" << std::endl;
            }
            else if (error == boost::asio::error::operation_aborted)
            {
                glog.is(DEBUG1) && glog << "Read operation aborted (socket closed)" << std::endl;
            }
            else
            {
                glog.is(WARN) && glog << "Error reading from serial connection: " << error
                                      << std::endl;
            }
        }
    }

    bool check_escapes(MessagePart part)
    {
        using goby::glog;
        using goby::common::logger::DEBUG1;
        using goby::common::logger::WARN;
        int escape = std::count(message_.begin(), message_.end(), SV2_ESCAPE);
        if (escape != last_escape_)
        {
            boost::asio::async_read(
                socket_, buffer_, boost::asio::transfer_exactly(escape - last_escape_),
                boost::bind(&SV2SerialConnection::handle_read, this, _1, _2, part));
            glog.is(DEBUG1) && glog << "Reading " << escape - last_escape_
                                    << " more bytes because of escape characters." << std::endl;

            last_escape_ = escape;
            return false;
        }
        else
        {
            return true;
        }
    }

    void remove_escapes()
    {
        if (last_escape_ == 0)
            return;

        // remove escape characters - order matters (or else 0x7d5d5e would become 0x7e)
        int before_size = message_.size();
        boost::replace_all(message_, std::string(1, SV2_ESCAPE) + std::string(1, 0x5E),
                           std::string(1, SV2_MAGIC_BYTE));
        boost::replace_all(message_, std::string(1, SV2_ESCAPE) + std::string(1, 0x5D),
                           std::string(1, SV2_ESCAPE));
        int after_size = message_.size();

        message_insert_ -= (before_size - after_size);
        message_.resize(before_size);
        last_escape_ = std::count(message_.begin(), message_.end(), SV2_ESCAPE);
    }

    void add_escapes(std::string* message)
    {
        // add escape characters - order matters (or else 0x7e becomes 0x7d5d5e)
        boost::replace_all(*message, std::string(1, SV2_ESCAPE),
                           std::string(1, SV2_ESCAPE) + std::string(1, 0x5D));
        boost::replace_all(*message, std::string(1, SV2_MAGIC_BYTE),
                           std::string(1, SV2_ESCAPE) + std::string(1, 0x5E));
        // restore the actual start of frame
        boost::replace_first(*message, std::string(1, SV2_ESCAPE) + std::string(1, 0x5E),
                             std::string(1, SV2_MAGIC_BYTE));
    }

  private:
    boost::asio::serial_port socket_;
    boost::asio::streambuf buffer_;
    std::vector<char> message_;
    char* message_insert_;
    int message_size_;
    int last_escape_;

    enum
    {
        SV2_MAGIC_BYTE = 0x7e,
        SV2_ESCAPE = 0x7d
    };
    enum
    {
        SV2_HEADER_SIZE = 10
    }; // not including magic byte
    enum
    {
        SV2_MAX_SIZE = 557
    };
};
} // namespace moos
} // namespace goby
