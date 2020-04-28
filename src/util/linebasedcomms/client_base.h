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

#ifndef ClientBase20100628H
#define ClientBase20100628H

#include "connection.h"
#include "interface.h"

namespace goby
{
namespace util
{
// template for type of client socket (asio::serial_port, asio::ip::tcp::socket)
// LineBasedInterface runs in a one thread (same as user program)
// LineBasedClient and LineBasedConnection run in another thread (spawned by LineBasedInterface's constructor)
template <typename ASIOAsyncReadStream>
class LineBasedClient : public LineBasedInterface, public LineBasedConnection<ASIOAsyncReadStream>
{
  protected:
    LineBasedClient(const std::string& delimiter, int retry_interval = 10)
        : LineBasedInterface(delimiter), LineBasedConnection<ASIOAsyncReadStream>(this),
          retry_interval_(retry_interval)
    {
    }

    virtual ~LineBasedClient() {}

    virtual ASIOAsyncReadStream& socket() = 0;
    virtual std::string local_endpoint() = 0;
    virtual std::string remote_endpoint() = 0;

    // from LineBasedInterface
    void do_start()
    {
        last_start_time_ = time::SystemClock::now<boost::posix_time::ptime>();

        set_active(start_specific());

        LineBasedInterface::io_context().post(
            boost::bind(&LineBasedConnection<ASIOAsyncReadStream>::read_start, this));
        if (!LineBasedConnection<ASIOAsyncReadStream>::out().empty())
            LineBasedInterface::io_context().post(
                boost::bind(&LineBasedConnection<ASIOAsyncReadStream>::write_start, this));
    }

    virtual bool start_specific() = 0;

    // from LineBasedInterface
    void do_write(const protobuf::Datagram& line)
    {
        bool write_in_progress = !LineBasedConnection<ASIOAsyncReadStream>::out().empty();
        LineBasedConnection<ASIOAsyncReadStream>::out().push_back(line);
        if (!write_in_progress)
            LineBasedInterface::io_context().post(
                boost::bind(&LineBasedConnection<ASIOAsyncReadStream>::write_start, this));
    }

    // from LineBasedInterface
    void do_close(const boost::system::error_code& error)
    {
        if (error ==
            boost::asio::error::operation_aborted) // if this call is the result of a timer cancel()
            return; // ignore it because the connection cancelled the timer

        set_active(false);
        socket().close();

        // try to restart if we had a real error
        if (error != boost::system::error_code())
        {
            using namespace boost::posix_time;
            ptime now = time::SystemClock::now<ptime>();
            if (now - seconds(retry_interval_) < last_start_time_)
                LineBasedInterface::sleep(retry_interval_ -
                                          (now - last_start_time_).total_seconds());

            // add this to the io_context jobs
            LineBasedInterface::start();
        }
    }

    // same as do_close in this case
    void socket_close(const boost::system::error_code& error) { do_close(error); }

  private:
    boost::posix_time::ptime last_start_time_;

    LineBasedClient(const LineBasedClient&);
    LineBasedClient& operator=(const LineBasedClient&);

    int retry_interval_;
};

} // namespace util
} // namespace goby

#endif
