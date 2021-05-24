
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

bool operator==(const RMC& rmc1, const RMC& rmc2)
{
    return rmc1.serialize().message() == rmc2.serialize().message();
}

std::ostream& operator<<(std::ostream& os, const RMC& rmc)
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

bool operator==(const HDT& hdt1, const HDT& hdt2)
{
    return hdt1.serialize().message() == hdt2.serialize().message();
}

std::ostream& operator<<(std::ostream& os, const HDT& hdt)
{
    return (os << hdt.serialize().message());
}

} // namespace gps
} // namespace util
} // namespace goby

#endif
