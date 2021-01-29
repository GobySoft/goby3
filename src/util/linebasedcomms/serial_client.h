// Copyright 2009-2021:
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

// Old interface - replaced for most uses by middleware/io/line_based/serial.h

#ifndef GOBY_UTIL_LINEBASEDCOMMS_SERIAL_CLIENT_H
#define GOBY_UTIL_LINEBASEDCOMMS_SERIAL_CLIENT_H

#include <atomic>
#include <memory>
#include <string> // for string, operator!=
#include <thread>

#include "goby/middleware/io/line_based/serial.h"
#include "goby/middleware/protobuf/io.pb.h"

#include "interface.h"

namespace goby
{
namespace util
{
/// provides a basic client for line by line text based communications over a 8N1 tty (such as an RS-232 serial link) without flow control
class SerialClient : public LineBasedInterface
{
  public:
    /// \brief create a serial client
    ///
    /// \param name name of the serial connection (e.g. "/dev/ttyS0")
    /// \param baud baud rate of the serial connection (e.g. 9600)
    /// \param delimiter string used to split lines
    SerialClient(std::string name = "", unsigned baud = 9600,
                 const std::string& delimiter = "\r\n");

    ~SerialClient() override;

    /// set serial port name, e.g. "/dev/ttyS0"
    void set_name(const std::string& name) { name_ = name; }
    /// baud rate, e.g. 4800
    void set_baud(unsigned baud) { baud_ = baud; }

    /// serial port name, e.g. "/dev/ttyS0"
    std::string name() const { return name_; }

    /// baud rate, e.g. 4800
    unsigned baud() const { return baud_; }

    /// our serial port, e.g. "/dev/ttyUSB1"
    std::string local_endpoint() override { return name_; }
    /// who knows where the serial port goes?! (empty string)
    std::string remote_endpoint() override { return ""; }

    void send_command(const middleware::protobuf::SerialCommand& command);
    const middleware::protobuf::SerialStatus& read_status() { return status_; }

  private:
    void do_start() override;
    void do_close() override;
    void do_subscribe() override;

  private:
    std::string name_;
    unsigned baud_;

    using Thread = goby::middleware::io::SerialThreadLineBased<
        groups::linebasedcomms_in, groups::linebasedcomms_out,
        goby::middleware::io::PubSubLayer::INTERTHREAD,
        goby::middleware::io::PubSubLayer::INTERTHREAD, goby::util::LineBasedCommsThreadStub, true>;

    std::atomic<bool> serial_alive_{false};
    std::unique_ptr<std::thread> serial_thread_;

    middleware::protobuf::SerialStatus status_;
};
} // namespace util
} // namespace goby
#endif
