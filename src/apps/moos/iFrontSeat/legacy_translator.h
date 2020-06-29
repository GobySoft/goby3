// Copyright 2013-2020:
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

// defines translations for backwards compatible use (hopefully temporary)
// with iHuxley
#ifndef LegacyTranslator20130222H
#define LegacyTranslator20130222H

#include "goby/moos/moos_header.h"

#include "goby/middleware/frontseat/interface.h"

namespace goby
{
namespace apps
{
namespace moos
{
class iFrontSeat;
class FrontSeatLegacyTranslator
{
  public:
    FrontSeatLegacyTranslator(iFrontSeat* fs);

  private:
    void handle_mail_ctd(const CMOOSMsg& msg);
    void handle_mail_desired_course(const CMOOSMsg& msg);
    enum ModemRawDirection
    {
        OUTGOING,
        INCOMING
    };
    void handle_mail_modem_raw(const CMOOSMsg& msg, ModemRawDirection direction);

    // all for buoyancy, trim, silent
    void handle_mail_buoyancy_control(const CMOOSMsg& msg);
    void handle_mail_trim_control(const CMOOSMsg& msg);
    void handle_mail_frontseat_bhvoff(const CMOOSMsg& msg);
    void handle_mail_frontseat_silent(const CMOOSMsg& msg);
    void handle_mail_backseat_abort(const CMOOSMsg& msg);

    void handle_driver_data_from_frontseat(
        const goby::middleware::frontseat::protobuf::InterfaceData& data);
    void set_fs_bs_ready_flags(goby::middleware::frontseat::protobuf::InterfaceState state);

    void publish_command(const goby::middleware::frontseat::protobuf::CommandRequest& command);

  private:
    iFrontSeat* ifs_;
    goby::middleware::frontseat::protobuf::CTDSample ctd_sample_;
    goby::middleware::frontseat::protobuf::DesiredCourse desired_course_;

    // added to each request we send so as not to conflict with other requestors
    enum
    {
        LEGACY_REQUEST_IDENTIFIER = 1 << 16
    };
    int request_id_;
};
} // namespace moos
} // namespace apps
} // namespace goby

#endif
