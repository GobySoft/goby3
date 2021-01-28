// Copyright 2010-2020:
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

#ifndef GOBY_UTIL_LINEBASEDCOMMS_INTERFACE_H
#define GOBY_UTIL_LINEBASEDCOMMS_INTERFACE_H

#include <deque>  // for deque
#include <memory> // for shared_ptr
#include <mutex>  // for mutex
#include <string> // for string
#include <thread> // for thread

#include <boost/bind.hpp> // for bind_t, list_av_1<...

#include "goby/middleware/group.h"
#include "goby/middleware/transport/interthread.h"
#include "goby/util/asio_compat.h"
#include "goby/util/linebasedcomms/thread_stub.h"
#include "goby/util/protobuf/linebasedcomms.pb.h" // for Datagram

namespace boost
{
namespace system
{
class error_code;
} // namespace system
} // namespace boost

namespace goby
{
namespace util
{
namespace groups
{
constexpr goby::middleware::Group linebasedcomms_in{"goby::util::LineBasedInterface::in"};
constexpr goby::middleware::Group linebasedcomms_out{"goby::util::LineBasedInterface::out"};
} // namespace groups

/// basic interface class for all the derived serial (and networking mimics) line-based nodes (serial, tcp, udp, etc.)
class LineBasedInterface
{
  public:
    LineBasedInterface(const std::string& delimiter);
    virtual ~LineBasedInterface();

    // start the connection
    void start();
    // close the connection cleanly
    void close();
    // is the connection alive and well?
    bool active()
    {
        // ensure we've received any status messages first
        interthread_.poll(std::chrono::seconds(0));
        return active_;
    }

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

  protected:
    virtual void do_start() = 0;
    virtual void do_close() = 0;

    virtual std::string local_endpoint() = 0;
    virtual std::string remote_endpoint() { return ""; };

    void set_active(bool active) { active_ = active; }

    std::string& delimiter() { return delimiter_; }
    std::deque<goby::util::protobuf::Datagram>& in() { return in_; }

    goby::middleware::InterThreadTransporter& interthread() { return interthread_; }

    int index() { return index_; }

    virtual void do_subscribe() = 0;

    goby::middleware::DynamicGroup& in_group() { return in_group_; }
    goby::middleware::DynamicGroup& out_group() { return out_group_; }

    bool io_thread_ready() { return io_thread_ready_; }

  private:
    void subscribe();
    void poll();

  private:
    std::string delimiter_;
    std::deque<protobuf::Datagram> in_; // buffered read data

    bool active_; // remains true while this object is still operating

    int index_;
    static std::atomic<int> count_;

    // DynamicGroups must outlive interthread_ since they hold the std::string used by the underlying Group
    goby::middleware::DynamicGroup in_group_;
    goby::middleware::DynamicGroup out_group_;

    goby::middleware::InterThreadTransporter interthread_;

    bool io_thread_ready_{false};

    std::thread::id current_thread_;
};

} // namespace util
} // namespace goby

#endif
