// Copyright 2011-2024:
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

#ifndef GOBY_ACOMMS_MODEMDRIVER_IRIDIUM_DRIVER_H
#define GOBY_ACOMMS_MODEMDRIVER_IRIDIUM_DRIVER_H

#include <cstdint> // for uint32_t
#include <iosfwd>  // for ostream
#include <memory>  // for shared_ptr

#include "goby/acomms/modemdriver/driver_base.h"    // for ModemDriverBase
#include "goby/acomms/protobuf/driver_base.pb.h"    // for DriverConfig
#include "goby/acomms/protobuf/iridium_driver.pb.h" // for Config, MessageT...
#include "goby/acomms/protobuf/modem_message.pb.h"  // for ModemTransmission
#include "iridium_driver_fsm.h"                     // for IridiumDriverFSM

namespace goby
{
namespace util
{
class TCPClient;
} // namespace util

namespace acomms
{
class IridiumDriver : public ModemDriverBase
{
  public:
    IridiumDriver();
    ~IridiumDriver() override;
    void startup(const protobuf::DriverConfig& cfg) override;

    void modem_init();

    void shutdown() override;
    void do_work() override;

    void handle_initiate_transmission(const protobuf::ModemTransmission& m) override;
    void report(goby::acomms::protobuf::ModemReport* report) override;

  private:
    void process_transmission(protobuf::ModemTransmission msg, bool dial);

    void receive(const protobuf::ModemTransmission& msg);
    void send(const protobuf::ModemTransmission& msg);

    void try_serial_tx();
    void display_state_cfg(std::ostream* os);

    void hangup();
    void set_dtr(bool state);
    bool query_dtr();

    const iridium::protobuf::Config& iridium_driver_cfg()
    {
        return driver_cfg_.GetExtension(iridium::protobuf::config);
    };

  private:
    protobuf::DriverConfig driver_cfg_;
    iridium::fsm::IridiumDriverFSM fsm_;

    std::shared_ptr<goby::util::TCPClient> debug_client_;

    double last_triple_plus_time_{0};
    enum
    {
        TRIPLE_PLUS_WAIT = 2
    };

    protobuf::ModemTransmission rudics_mac_msg_;

    std::uint32_t next_frame_{0};

    bool running_{false};
};
} // namespace acomms
} // namespace goby
#endif
