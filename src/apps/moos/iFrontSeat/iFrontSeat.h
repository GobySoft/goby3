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

#ifndef iFrontSeat20130220H
#define iFrontSeat20130220H

#include "goby/moos/goby_moos_app.h"

#include "goby/moos/frontseat/frontseat.h"
#include "goby/moos/protobuf/frontseat_config.pb.h"

#include "legacy_translator.h"

FrontSeatInterfaceBase* load_driver(iFrontSeatConfig*);

class iFrontSeat : public GobyMOOSApp
{
  public:
    static void* driver_library_handle_;
    static iFrontSeat* get_instance();

    friend class FrontSeatLegacyTranslator;

  private:
    iFrontSeat();
    ~iFrontSeat() {}
    iFrontSeat(const iFrontSeat&);
    iFrontSeat& operator=(const iFrontSeat&);

    // synchronous event
    void loop();
    void status_loop();

    // mail handlers
    void handle_mail_command_request(const CMOOSMsg& msg);
    void handle_mail_data_to_frontseat(const CMOOSMsg& msg);
    void handle_mail_raw_out(const CMOOSMsg& msg);
    void handle_mail_helm_state(const CMOOSMsg& msg);

    // frontseat driver signal handlers
    void handle_driver_command_response(const goby::moos::protobuf::CommandResponse& response);
    void
    handle_driver_data_from_frontseat(const goby::moos::protobuf::FrontSeatInterfaceData& data);
    void handle_driver_raw_in(const goby::moos::protobuf::FrontSeatRaw& data);
    void handle_driver_raw_out(const goby::moos::protobuf::FrontSeatRaw& data);

  private:
    boost::shared_ptr<FrontSeatInterfaceBase> frontseat_;

    FrontSeatLegacyTranslator translator_;

    static iFrontSeatConfig cfg_;
    static iFrontSeat* inst_;
};

#endif
