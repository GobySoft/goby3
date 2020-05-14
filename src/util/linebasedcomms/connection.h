// Copyright 2011-2019:
//   GobySoft, LLC (2013-)
//   Massachusetts Institute of Technology (2007-2014)
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

#ifndef ASIOLineBasedConnection20100715H
#define ASIOLineBasedConnection20100715H

#include "goby/time.h"
#include "goby/util/debug_logger.h"

#include "interface.h"

namespace goby
{
namespace util
{
// template for type of client socket (asio::serial_port, asio::ip::tcp::socket)

template <typename ASIOAsyncReadStream> class LineBasedConnection
{
  public:
    LineBasedConnection<ASIOAsyncReadStream>(LineBasedInterface* interface) : interface_(interface)
    {
    }
    virtual ~LineBasedConnection<ASIOAsyncReadStream>() {}

    virtual ASIOAsyncReadStream& socket() = 0;

    void read_start()
    {
        async_read_until(socket(), buffer_, interface_->delimiter(),
                         boost::bind(&LineBasedConnection::read_complete, this,
                                     boost::asio::placeholders::error));
    }

    void write_start()
    { // Start an asynchronous write and call write_complete when it completes or fails
        // don't write if it has a dest and the id doesn't match
        if (!(out_.front().has_dest() && out_.front().dest() != remote_endpoint()))
        {
            boost::asio::async_write(socket(), boost::asio::buffer(out_.front().data()),
                                     boost::bind(&LineBasedConnection::write_complete, this,
                                                 boost::asio::placeholders::error));
        }
        else
        {
            // discard message not for our remote endpoint
            out_.pop_front();
        }
    }

    void read_complete(const boost::system::error_code& error)
    {
        if (error == boost::asio::error::operation_aborted)
        {
            return;
        }
        else if (error)
        {
            //                    goby::glog.is(goby::util::logger::DEBUG2, goby::util::logger_lock::lock) &&
            //                        goby::glog << "Error on reading from socket: " << error.message() << std::endl << unlock;
            return socket_close(error);
        }

        std::istream is(&buffer_);
        std::string& line = *in_datagram_.mutable_data();

        if (!remote_endpoint().empty())
            in_datagram_.set_src(remote_endpoint());
        if (!local_endpoint().empty())
            in_datagram_.set_dest(local_endpoint());

        in_datagram_.set_time(goby::time::SystemClock::now<goby::time::SITime>().value());
        char last = interface_->delimiter().at(interface_->delimiter().length() - 1);
        std::getline(is, line, last);

        {
            std::lock_guard<std::mutex> lock(interface_->in_mutex());
            interface_->in().push_back(in_datagram_);
        }
        read_start(); // start waiting for another asynchronous read again
    }

    void write_complete(const boost::system::error_code& error)
    { // the asynchronous read operation has now completed or failed and returned an error
        if (error == boost::asio::error::operation_aborted)
        {
            return;
        }
        else if (error)
        {
            //                    goby::glog.is(goby::util::logger::DEBUG2, goby::util::logger_lock::lock) &&
            //                        goby::glog << "Error on writing from socket: " << error.message() << std::endl << unlock;

            return socket_close(error);
        }

        out_.pop_front();  // remove the completed data
        if (!out_.empty()) // if there is anthing left to be written
            write_start(); // then start sending the next item in the buffer
    }

    virtual void socket_close(const boost::system::error_code& error) = 0;

    virtual std::string local_endpoint() = 0;
    virtual std::string remote_endpoint() = 0;

    std::deque<protobuf::Datagram>& out() { return out_; }

  private:
    LineBasedInterface* interface_;
    boost::asio::streambuf buffer_;
    protobuf::Datagram in_datagram_;
    std::deque<protobuf::Datagram> out_; // buffered write data
};
} // namespace util
} // namespace goby

#endif
