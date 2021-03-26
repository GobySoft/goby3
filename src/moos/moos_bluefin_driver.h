// Copyright 2011-2021:
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

#ifndef GOBY_MOOS_MOOS_BLUEFIN_DRIVER_H
#define GOBY_MOOS_MOOS_BLUEFIN_DRIVER_H

#include <map>    // for map
#include <string> // for string

#include <MOOS/libMOOS/Comms/MOOSCommClient.h> // for CMOOSCommClient
#include <boost/bimap.hpp>                     // for bimap

#include "goby/acomms/modemdriver/driver_base.h"   // for ModemDriverBase
#include "goby/acomms/protobuf/driver_base.pb.h"   // for DriverConfig
#include "goby/acomms/protobuf/modem_message.pb.h" // for ModemTransmission
#include "goby/moos/protobuf/bluefin_driver.pb.h"  // for Config

namespace goby
{
namespace acomms
{
class MACManager;
} // namespace acomms
namespace util
{
class NMEASentence;
} // namespace util

namespace moos
{
/// \brief provides a driver for the Bluefin Huxley communications infrastructure (initially uses SonarDyne as underlying hardware)
/// \ingroup acomms_api
///
class BluefinCommsDriver : public goby::acomms::ModemDriverBase
{
  public:
    BluefinCommsDriver(goby::acomms::MACManager* mac);
    void startup(const goby::acomms::protobuf::DriverConfig& cfg) override;
    void shutdown() override;
    void do_work() override;
    void handle_initiate_transmission(const goby::acomms::protobuf::ModemTransmission& m) override;

  private:
    std::string unix_time2nmea_time(double time);
    void bfcma(const goby::util::NMEASentence& nmea);
    void bfcps(const goby::util::NMEASentence& nmea);
    void bfcpr(const goby::util::NMEASentence& nmea);

  private:
    CMOOSCommClient moos_client_;
    goby::acomms::protobuf::DriverConfig driver_cfg_; // configuration given to you at launch
    goby::moos::bluefin::protobuf::Config bluefin_driver_cfg_;

    std::string current_modem_;
    double end_of_mac_window_;

    // modem name to map of rate to bytes
    std::map<std::string, std::map<int, int> > modem_to_rate_to_bytes_;

    // maps goby modem id to bluefin modem id
    boost::bimap<int, int> goby_to_bluefin_id_;

    goby::acomms::MACManager* mac_;

    int last_request_id_;
    goby::acomms::protobuf::ModemTransmission last_data_msg_;
};
} // namespace moos
} // namespace goby

#endif
