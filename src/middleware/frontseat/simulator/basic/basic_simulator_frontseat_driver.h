// Copyright 2009-2018 Toby Schneider (http://gobysoft.org/index.wt/people/toby)
//                     GobySoft, LLC (2013-)
//                     Massachusetts Institute of Technology (2007-2014)
//
//
// This file is part of the Goby Underwater Autonomy Project Binaries
// ("The Goby Binaries").
//
// The Goby Binaries are free software: you can redistribute them and/or modify
// them under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 2 of the License, or
// (at your option) any later version.
//
// The Goby Binaries are distributed in the hope that they will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with Goby.  If not, see <http://www.gnu.org/licenses/>.

#ifndef BasicSimulatorFrontSeat20130220H
#define BasicSimulatorFrontSeat20130220H

#include <boost/bimap.hpp>

#include "goby/middleware/frontseat/interface.h"
#include "goby/time/system_clock.h"
#include "goby/util/linebasedcomms/tcp_client.h"

#include "basic_simulator_frontseat_driver_config.pb.h"

extern "C"
{
    goby::middleware::frontseat::InterfaceBase*
    frontseat_driver_load(goby::middleware::frontseat::protobuf::Config* cfg);
}

namespace goby
{
namespace middleware
{
namespace frontseat
{
class BasicSimulatorFrontSeatInterface : public InterfaceBase
{
  public:
    BasicSimulatorFrontSeatInterface(const protobuf::Config& cfg);

  private: // virtual methods from FrontSeatInterfaceBase
    void loop() override;

    void send_command_to_frontseat(const protobuf::CommandRequest& command) override;
    void send_data_to_frontseat(const protobuf::InterfaceData& data) override;
    void send_raw_to_frontseat(const protobuf::Raw& data) override;
    protobuf::FrontSeatState frontseat_state() const override;
    bool frontseat_providing_data() const override;

  private: // internal non-virtual methods
    void check_connection_state();

    void try_receive();
    void process_receive(const std::string& s);
    void parse_in(const std::string& in, std::map<std::string, std::string>* out);

    void write(const std::string& s);

  private:
    const protobuf::BasicSimulatorFrontSeatConfig sim_config_;
    goby::util::TCPClient tcp_;

    bool frontseat_providing_data_;
    goby::time::SystemClock::time_point last_frontseat_data_time_;
    protobuf::FrontSeatState frontseat_state_;

    protobuf::CommandRequest last_request_;
};
} // namespace frontseat
} // namespace middleware
} // namespace goby

#endif
