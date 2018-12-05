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

#ifndef IverFrontSeat20171117H
#define IverFrontSeat20171117H

#include <boost/bimap.hpp>
#include <boost/units/quantity.hpp>
#include <boost/units/systems/angle/degrees.hpp>
#include <boost/units/systems/si.hpp>
#include <boost/units/systems/si/prefixes.hpp>

#include "goby/util/linebasedcomms/serial_client.h"
#include "goby/util/primitive_types.h"

#include "goby/moos/frontseat/frontseat.h"

#include "iver_driver_config.pb.h"

extern "C"
{
    FrontSeatInterfaceBase* frontseat_driver_load(iFrontSeatConfig*);
}

class IverFrontSeat : public FrontSeatInterfaceBase
{
  public:
    IverFrontSeat(const iFrontSeatConfig& cfg);

  private: // virtual methods from FrontSeatInterfaceBase
    void loop();

    void send_command_to_frontseat(const goby::moos::protobuf::CommandRequest& command);
    void send_data_to_frontseat(const goby::moos::protobuf::FrontSeatInterfaceData& data);
    void send_raw_to_frontseat(const goby::moos::protobuf::FrontSeatRaw& data);
    goby::moos::protobuf::FrontSeatState frontseat_state() const;
    bool frontseat_providing_data() const;

  private: // internal non-virtual methods
    void check_connection_state();
    void try_receive();
    void process_receive(const std::string& s);
    void write(const std::string& s);

    // given a time and date in "NMEA form", returns the value as seconds since the start of the epoch (1970-01-01 00:00:00Z)
    // NMEA form for time is HHMMSS where "H" is hours, "M" is minutes, "S" is seconds or fractional seconds
    // NMEA form for date is DDMMYY where "D" is day, "M" is month, "Y" is year
    boost::units::quantity<boost::units::si::time> nmea_time_to_seconds(double nmea_time,
                                                                        int nmea_date);

    // given a latitude or longitude in "NMEA form" and the hemisphere character ('N', 'S', 'E' or 'W'), returns the value as decimal degrees
    // NMEA form is DDDMM.MMMM or DDMM.MMMM where "D" is degrees, and "M" is minutes
    boost::units::quantity<boost::units::degree::plane_angle> nmea_geo_to_degrees(double nmea_geo,
                                                                                  char hemi);

  private:
    const IverFrontSeatConfig iver_config_;
    goby::util::SerialClient serial_;

    boost::shared_ptr<goby::util::SerialClient> ntp_serial_;

    bool frontseat_providing_data_;
    double last_frontseat_data_time_;
    goby::moos::protobuf::FrontSeatState frontseat_state_;

    goby::moos::protobuf::CommandRequest last_request_;

    goby::moos::protobuf::NodeStatus status_;
};

#endif
