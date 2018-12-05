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

#ifndef IridiumShoreSBD20150227H
#define IridiumShoreSBD20150227H

#include "goby/acomms/protobuf/iridium_sbd_directip.pb.h"
#include "goby/acomms/protobuf/rudics_shore.pb.h"
#include "goby/common/time.h"
#include "goby/util/binary.h"
#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include <boost/enable_shared_from_this.hpp>

namespace goby
{
namespace acomms
{
class SBDMessageReader
{
  public:
    SBDMessageReader(boost::asio::ip::tcp::socket& socket)
        : socket_(socket), pos_(0), data_(1 << 16)
    {
    }

    virtual bool data_ready() const = 0;

    const goby::acomms::protobuf::DirectIPMOPreHeader& pre_header() const { return pre_header_; }
    const goby::acomms::protobuf::DirectIPMOHeader& header() const { return header_; }
    const goby::acomms::protobuf::DirectIPMOPayload& body() const { return body_; }
    const goby::acomms::protobuf::DirectIPMTConfirmation& confirm() const { return confirm_; }

    enum
    {
        PRE_HEADER_SIZE = 3,
        BITS_PER_BYTE = 8
    };

    void pre_header_handler(const boost::system::error_code& error, std::size_t bytes_transferred)
    {
        if (error)
            throw std::runtime_error("Error while reading: " + error.message());

        pre_header_.set_protocol_ver(read_byte());
        pre_header_.set_overall_length(read_uint16());

        boost::asio::async_read(socket_, boost::asio::buffer(data_),
                                boost::asio::transfer_at_least(pre_header_.overall_length() +
                                                               PRE_HEADER_SIZE - bytes_transferred),
                                boost::bind(&SBDMessageReader::ie_handler, this, _1, _2));
    }

    std::vector<char>& data() { return data_; }

  private:
    void ie_handler(const boost::system::error_code& error, std::size_t bytes_transferred)
    {
        if (error)
            throw std::runtime_error("Error while reading: " + error.message());

        char iei = read_byte();
        unsigned length = read_uint16();

        switch (iei)
        {
            case 0x01: // header
            {
                header_.set_iei(iei);
                header_.set_length(length);
                header_.set_cdr_reference(read_uint32()); // 4 bytes

                header_.set_imei(read_imei()); // 15 bytes

                header_.set_session_status(read_byte());    // 1 byte
                header_.set_momsn(read_uint16());           // 2 bytes
                header_.set_mtmsn(read_uint16());           // 2 bytes
                header_.set_time_of_session(read_uint32()); // 4 bytes
            }
            break;

            case 0x02: // payload
                body_.set_iei(iei);
                body_.set_length(length);
                body_.set_payload(std::string(data_.begin() + pos_, data_.begin() + pos_ + length));
                pos_ += length;
                break;

            case 0x44: // confirmation
                confirm_.set_iei(iei);
                confirm_.set_length(length);
                confirm_.set_client_id(read_uint32());
                confirm_.set_imei(read_imei());
                confirm_.set_auto_ref_id(read_uint32());
                confirm_.set_status(read_int16());

            default: // skip this IE
                pos_ += length;
                break;
        }

        if (pos_ < pre_header_.overall_length() + PRE_HEADER_SIZE)
            ie_handler(error, bytes_transferred);
    }

    char read_byte() { return data_.at(pos_++) & 0xff; }

    unsigned read_uint16()
    {
        unsigned u = (data_.at(pos_++) & 0xff) << BITS_PER_BYTE;
        u |= data_.at(pos_++) & 0xff;
        return u;
    }

    int read_int16()
    {
        int i = (data_.at(pos_++) & 0xff) << BITS_PER_BYTE;
        i |= data_.at(pos_++) & 0xff;

        // sign extend
        int sign_bit_mask = 0x8000;
        if (i & sign_bit_mask)
            i |= (-1 - 0xFFFF); // extend bits to fill int

        return i;
    }

    unsigned read_uint32()
    {
        unsigned u = 0;
        for (int i = 0; i < 4; ++i) u |= (data_.at(pos_++) & 0xff) << ((3 - i) * BITS_PER_BYTE);
        return u;
    }

    std::string read_imei()
    {
        const int imei_size = 15;
        std::string imei = std::string(data_.begin() + pos_, data_.begin() + pos_ + imei_size);
        pos_ += imei_size;
        return imei;
    }

  private:
    goby::acomms::protobuf::DirectIPMOPreHeader pre_header_;
    goby::acomms::protobuf::DirectIPMOHeader header_;
    goby::acomms::protobuf::DirectIPMOPayload body_;
    goby::acomms::protobuf::DirectIPMTConfirmation confirm_;

    boost::asio::ip::tcp::socket& socket_;

    std::vector<char>::size_type pos_;
    std::vector<char> data_;
};

class SBDMOMessageReader : public SBDMessageReader
{
  public:
    SBDMOMessageReader(boost::asio::ip::tcp::socket& socket) : SBDMessageReader(socket) {}

    bool data_ready() const
    {
        return pre_header().IsInitialized() && header().IsInitialized() && body().IsInitialized();
    }
};

class SBDMTConfirmationMessageReader : public SBDMessageReader
{
  public:
    SBDMTConfirmationMessageReader(boost::asio::ip::tcp::socket& socket) : SBDMessageReader(socket)
    {
    }

    bool data_ready() const { return pre_header().IsInitialized() && confirm().IsInitialized(); }
};

class SBDConnection : public boost::enable_shared_from_this<SBDConnection>
{
  public:
    static boost::shared_ptr<SBDConnection> create(boost::asio::io_service& io_service)
    {
        return boost::shared_ptr<SBDConnection>(new SBDConnection(io_service));
    }

    boost::asio::ip::tcp::socket& socket() { return socket_; }

    void start()
    {
        remote_endpoint_str_ = boost::lexical_cast<std::string>(socket_.remote_endpoint());

        connect_time_ = goby::common::goby_time<double>();
        boost::asio::async_read(
            socket_, boost::asio::buffer(message_.data()),
            boost::asio::transfer_at_least(SBDMessageReader::PRE_HEADER_SIZE),
            boost::bind(&SBDMessageReader::pre_header_handler, &message_, _1, _2));
    }

    ~SBDConnection() {}

    double connect_time() const { return connect_time_; }

    const SBDMOMessageReader& message() const { return message_; }
    const std::string& remote_endpoint_str() { return remote_endpoint_str_; }

  private:
    SBDConnection(boost::asio::io_service& io_service)
        : socket_(io_service), connect_time_(-1), message_(socket_), remote_endpoint_str_("Unknown")
    {
    }

    boost::asio::ip::tcp::socket socket_;
    double connect_time_;
    SBDMOMessageReader message_;
    std::string remote_endpoint_str_;
};

class SBDServer
{
  public:
    SBDServer(boost::asio::io_service& io_service, int port)
        : acceptor_(io_service, boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), port))
    {
        start_accept();
    }

    std::set<boost::shared_ptr<SBDConnection> >& connections() { return connections_; }

  private:
    void start_accept()
    {
        boost::shared_ptr<SBDConnection> new_connection =
            SBDConnection::create(acceptor_.get_io_service());

        connections_.insert(new_connection);

        acceptor_.async_accept(new_connection->socket(),
                               boost::bind(&SBDServer::handle_accept, this, new_connection,
                                           boost::asio::placeholders::error));
    }

    void handle_accept(boost::shared_ptr<SBDConnection> new_connection,
                       const boost::system::error_code& error)
    {
        if (!error)
        {
            using namespace goby::common::logger;
            using goby::glog;

            glog.is(DEBUG1) && glog << "Received SBD connection from: "
                                    << new_connection->socket().remote_endpoint() << std::endl;

            new_connection->start();
        }

        start_accept();
    }

    std::set<boost::shared_ptr<SBDConnection> > connections_;
    boost::asio::ip::tcp::acceptor acceptor_;
};

} // namespace acomms
} // namespace goby

#endif
