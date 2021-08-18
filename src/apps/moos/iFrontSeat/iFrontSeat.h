// Copyright 2013-2021:
//   GobySoft, LLC (2013-)
//   Massachusetts Institute of Technology (2007-2014)
//   Community contributors (see AUTHORS file)
// File authors:
//   Toby Schneider <toby@gobysoft.org>
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

#ifndef GOBY_APPS_MOOS_IFRONTSEAT_IFRONTSEAT_H
#define GOBY_APPS_MOOS_IFRONTSEAT_IFRONTSEAT_H

#include <memory> // for unique_ptr

#include "goby/middleware/frontseat/interface.h" // for InterfaceBase
#include "goby/moos/goby_moos_app.h"             // for GobyMOOSApp
#include "legacy_translator.h"                   // for FrontSeatLegacyTran...

class CMOOSMsg;

namespace goby
{
namespace middleware
{
namespace frontseat
{
namespace protobuf
{
class CommandResponse;
class InterfaceData;
class Raw;
} // namespace protobuf
} // namespace frontseat
} // namespace middleware

namespace apps
{
namespace moos
{
namespace protobuf
{
class iFrontSeatConfig;
} // namespace protobuf

class iFrontSeat : public goby::moos::GobyMOOSApp
{
  public:
    static void* driver_library_handle_;
    static iFrontSeat* get_instance();

    friend class goby::apps::moos::FrontSeatLegacyTranslator;

  private:
    iFrontSeat();
    ~iFrontSeat() override = default;
    iFrontSeat(const iFrontSeat&) = delete;
    iFrontSeat& operator=(const iFrontSeat&) = delete;

    // synchronous event
    void loop() override;
    void status_loop();

    // mail handlers
    void handle_mail_command_request(const CMOOSMsg& msg);
    void handle_mail_data_to_frontseat(const CMOOSMsg& msg);
    void handle_mail_raw_out(const CMOOSMsg& msg);
    void handle_mail_helm_state(const CMOOSMsg& msg);

    // frontseat driver signal handlers
    void handle_driver_command_response(
        const goby::middleware::frontseat::protobuf::CommandResponse& response);
    void handle_driver_data_from_frontseat(
        const goby::middleware::frontseat::protobuf::InterfaceData& data);
    void handle_driver_raw_in(const goby::middleware::frontseat::protobuf::Raw& data);
    void handle_driver_raw_out(const goby::middleware::frontseat::protobuf::Raw& data);
    void handle_lat_origin(const CMOOSMsg& msg);
    void handle_lon_origin(const CMOOSMsg& msg);
    
  private:
    std::unique_ptr<goby::middleware::frontseat::InterfaceBase> frontseat_;

    FrontSeatLegacyTranslator translator_;

    static protobuf::iFrontSeatConfig cfg_;
    static iFrontSeat* inst_;

    double lat_origin_;
    double lon_origin_;
    bool new_origin_;
};
} // namespace moos
} // namespace apps
} // namespace goby

#endif
