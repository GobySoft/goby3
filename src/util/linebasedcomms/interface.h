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

#ifndef ASIOLineBasedInterface20100715H
#define ASIOLineBasedInterface20100715H

#include <deque>
#include <fstream>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>

#include <boost/array.hpp>
#include <boost/asio.hpp>
#include <boost/bind.hpp>

#include "goby/common/time.h"
#include "goby/util/protobuf/linebasedcomms.pb.h"

namespace goby
{
namespace util
{
/// basic interface class for all the derived serial (and networking mimics) line-based nodes (serial, tcp, udp, etc.)
class LineBasedInterface
{
  public:
    LineBasedInterface(const std::string& delimiter);
    virtual ~LineBasedInterface() {}

    // start the connection
    void start();
    // close the connection cleanly
    void close();
    // is the connection alive and well?
    bool active() { return active_; }

    void sleep(int sec);

    enum AccessOrder
    {
        NEWEST_FIRST,
        OLDEST_FIRST
    };

    /// \brief returns string line (including delimiter)
    ///
    /// \return true if data was read, false if no data to read
    bool readline(std::string* s, AccessOrder order = OLDEST_FIRST);
    bool readline(protobuf::Datagram* msg, AccessOrder order = OLDEST_FIRST);

    // write a line to the buffer
    void write(const std::string& s)
    {
        protobuf::Datagram datagram;
        datagram.set_data(s);
        write(datagram);
    }

    void write(const protobuf::Datagram& msg);

    // empties the read buffer
    void clear();

    void set_delimiter(const std::string& s) { delimiter_ = s; }
    std::string delimiter() const { return delimiter_; }

    boost::asio::io_service& io_service() { return io_service_; }

  protected:
    // all implementors of this line based interface must provide do_start, do_write, do_close, and put all read data into "in_"
    virtual void do_start() = 0;
    virtual void do_write(const protobuf::Datagram& line) = 0;
    virtual void do_close(const boost::system::error_code& error) = 0;

    void set_active(bool active) { active_ = active; }

    std::string delimiter_;
    boost::asio::io_service io_service_; // the main IO service that runs this connection
    std::deque<protobuf::Datagram> in_;  // buffered read data
    std::mutex in_mutex_;

    template <typename ASIOAsyncReadStream> friend class LineBasedConnection;

    std::string& delimiter() { return delimiter_; }
    std::deque<goby::util::protobuf::Datagram>& in() { return in_; }
    std::mutex& in_mutex() { return in_mutex_; }

  private:
    class IOLauncher
    {
      public:
        IOLauncher(boost::asio::io_service& io_service)
            : io_service_(io_service), t_(boost::bind(&boost::asio::io_service::run, &io_service))
        {
        }

        ~IOLauncher()
        {
            io_service_.stop();
            t_.join();
        }

      private:
        boost::asio::io_service& io_service_;
        std::thread t_;
    };

    std::shared_ptr<IOLauncher> io_launcher_;

    boost::asio::io_service::work work_;
    bool active_; // remains true while this object is still operating
};

} // namespace util
} // namespace goby

#endif
