// Copyright 2021:
//   GobySoft, LLC (2013-)
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

#ifndef GOBY_UTIL_LINEBASEDCOMMS_GPS_SENTENCE_H
#define GOBY_UTIL_LINEBASEDCOMMS_GPS_SENTENCE_H

#include <boost/format.hpp>
#include <boost/optional.hpp>
#include <boost/units/base_units/metric/knot.hpp>
#include <boost/units/systems/angle/degrees.hpp>
#include <boost/units/systems/si.hpp>
#include <boost/units/systems/temperature/celsius.hpp>

#include "goby/time/convert.h"
#include "goby/time/system_clock.h"
#include "nmea_sentence.h"

namespace goby
{
namespace util
{
namespace gps
{
/// \brief Convert NMEA latitude/longitude string plus hemisphere to a decimal degrees value
double nmea_geo_to_decimal(std::string nmea_geo_str, char hemi);

enum CoordType
{
    LAT,
    LON
};

/// \brief Convert a decimal degrees latitude or longitude into a pair of NMEA string representation and hemisphere value
std::pair<std::string, char> decimal_to_nmea_geo(double decimal, CoordType t);

struct RMC
{
  public:
    RMC() = default;

    RMC(const NMEASentence& sentence) { parse(sentence); }

    void parse(const NMEASentence& sentence);
    NMEASentence serialize(std::string talker_id = "GP", int num_fields = min_size) const;

    boost::optional<goby::time::SystemClock::time_point> time;

    boost::optional<boost::units::quantity<boost::units::degree::plane_angle>> latitude;
    boost::optional<boost::units::quantity<boost::units::degree::plane_angle>> longitude;
    boost::optional<boost::units::quantity<boost::units::si::velocity>> speed_over_ground;
    boost::optional<boost::units::quantity<boost::units::degree::plane_angle>> course_over_ground;

    boost::optional<boost::units::quantity<boost::units::degree::plane_angle>> magnetic_variation;

    enum Fields
    {
        UTC_TIME = 1,
        VALIDITY = 2,
        LATITUDE = 3,
        LATITUDE_NS = 4,
        LONGITUDE = 5,
        LONGITUDE_EW = 6,
        SPEED_OVER_GROUND = 7,
        COURSE_OVER_GROUND = 8,
        DATE = 9,
        MAGNETIC_VARIATION = 10,
        MAG_VARIATION_EW = 11,
        MODE = 12,      // NMEA 2.3 and later
        NAV_STATUS = 13 // NMEA 4.1 and later
    };

    enum Status
    {
        DataValid,
        NavigationReceiverWarning
    };

    boost::optional<Status> status;

    constexpr static int min_size = MAG_VARIATION_EW + 1; // MAG_VARIATION_EW + talker
};

inline bool operator==(const RMC& rmc1, const RMC& rmc2)
{
    return rmc1.serialize().message() == rmc2.serialize().message();
}

inline std::ostream& operator<<(std::ostream& os, const RMC& rmc)
{
    return (os << rmc.serialize().message());
}

struct HDT
{
  public:
    HDT() = default;

    HDT(const NMEASentence& sentence) { parse(sentence); }

    void parse(const NMEASentence& sentence);
    NMEASentence serialize(std::string talker_id = "GP") const;

    boost::optional<boost::units::quantity<boost::units::degree::plane_angle>> true_heading;

    enum Fields
    {
        HEADING = 1,
        T = 2
    };
    constexpr static int min_size = HEADING + 1; // HEADING + talker
    constexpr static int size = T + 1;
};

inline bool operator==(const HDT& hdt1, const HDT& hdt2)
{
    return hdt1.serialize().message() == hdt2.serialize().message();
}

inline std::ostream& operator<<(std::ostream& os, const HDT& hdt)
{
    return (os << hdt.serialize().message());
}

struct WPL
{
  public:
    WPL() = default;

    WPL(const NMEASentence& sentence) { parse(sentence); }

    void parse(const NMEASentence& sentence);
    NMEASentence serialize(std::string talker_id = "EC") const;

    boost::optional<boost::units::quantity<boost::units::degree::plane_angle>> latitude;
    boost::optional<boost::units::quantity<boost::units::degree::plane_angle>> longitude;
    boost::optional<std::string> name;

    enum Fields
    {
        LATITUDE = 1,
        LATITUDE_NS = 2,
        LONGITUDE = 3,
        LONGITUDE_EW = 4,
        NAME = 5
    };
    constexpr static int min_size = NAME + 1; // NAME + talker
    constexpr static int size = min_size;
};

inline bool operator==(const WPL& wpl1, const WPL& wpl2)
{
    return wpl1.serialize().message() == wpl2.serialize().message();
}

inline std::ostream& operator<<(std::ostream& os, const WPL& wpl)
{
    return (os << wpl.serialize().message());
}

struct RTE
{
  public:
    RTE() = default;

    RTE(const NMEASentence& sentence) { parse(sentence); }

    void parse(const NMEASentence& sentence);
    NMEASentence serialize(std::string talker_id = "EC") const;

    boost::optional<std::string> name;
    std::vector<std::string> waypoint_names;
    boost::optional<int> total_number_sentences;
    boost::optional<int> current_sentence_index; // starting at 1
    enum RouteType
    {
        ROUTE_TYPE__INVALID,
        ROUTE_TYPE__COMPLETE = 'c',
        ROUTE_TYPE__WORKING_ROUTE = 'w'
    };
    RouteType type{ROUTE_TYPE__INVALID};

    enum Fields
    {
        TOTAL_NUMBER_SENTENCES = 1,
        CURRENT_SENTENCE_INDEX = 2,
        ROUTE_TYPE = 3,
        NAME = 4,
        FIRST_WAYPOINT_NAME = 5
        // then waypoint names until end of sentence
    };
    constexpr static int min_size = FIRST_WAYPOINT_NAME + 1; // FIRST_WAYPOINT_NAME + talker
};

inline bool operator==(const RTE& rte1, const RTE& rte2)
{
    return rte1.serialize().message() == rte2.serialize().message();
}

inline std::ostream& operator<<(std::ostream& os, const RTE& rte)
{
    return (os << rte.serialize().message());
}

} // namespace gps
} // namespace util
} // namespace goby

#endif
