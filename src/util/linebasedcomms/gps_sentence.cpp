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

#include "gps_sentence.h"

double goby::util::gps::nmea_geo_to_decimal(std::string nmea_geo_str, char hemi)
{
    double nmea_geo = goby::util::as<double>(nmea_geo_str);
    // DDMM.MMMM
    double deg_int = std::floor(nmea_geo / 1e2);
    double deg_frac = (nmea_geo - (deg_int * 1e2)) / 60;

    double sign = 1;
    if (hemi == 'S' || hemi == 'W')
        sign = -1;

    return sign * (deg_int + deg_frac);
}

std::pair<std::string, char> goby::util::gps::decimal_to_nmea_geo(double decimal,
                                                                  goby::util::gps::CoordType t)
{
    std::pair<std::string, char> result;
    int degrees = abs(static_cast<int>(decimal));
    double minutes = (std::fabs(decimal) - degrees) * 60;
    result.first = (t == LAT) ? boost::str(boost::format("%02i%08.5f") % degrees % minutes)
                              : boost::str(boost::format("%03i%08.5f") % degrees % minutes);

    if (decimal < 0.0)
        result.second = (t == LAT) ? 'S' : 'W';
    else
        result.second = (t == LAT) ? 'N' : 'E';
    return result;
}

void goby::util::gps::RMC::parse(const NMEASentence& sentence)
{
    if (sentence.size() >= min_size)
    {
        if (!sentence.at(UTC_TIME).empty() && !sentence.at(DATE).empty())
        {
            time = goby::time::convert_from_nmea<goby::time::SystemClock::time_point>(
                sentence.at(UTC_TIME), sentence.at(DATE));
        }

        if (!sentence.at(VALIDITY).empty())
        {
            if (sentence.at(VALIDITY) == "A")
            {
                status = RMC::Status::DataValid;
            }
            else if (sentence.at(VALIDITY) == "V")
            {
                status = RMC::Status::NavigationReceiverWarning;
            }
        }
        if ((!sentence.at(LATITUDE).empty()) && (!sentence.at(LATITUDE_NS).empty()))
        {
            latitude = nmea_geo_to_decimal(sentence.at(LATITUDE), sentence.as<char>(LATITUDE_NS)) *
                       boost::units::degree::degree;
        }

        if ((!sentence.at(LONGITUDE).empty()) && (!sentence.at(LONGITUDE_EW).empty()))
        {
            longitude =
                nmea_geo_to_decimal(sentence.at(LONGITUDE), sentence.as<char>(LONGITUDE_EW)) *
                boost::units::degree::degree;
        }

        if (!sentence.at(SPEED_OVER_GROUND).empty())
        {
            boost::units::metric::knot_base_unit::unit_type knots;
            speed_over_ground = boost::units::quantity<boost::units::si::velocity>(
                sentence.as<double>(SPEED_OVER_GROUND) * knots);
        }

        if (!sentence.at(COURSE_OVER_GROUND).empty())
        {
            course_over_ground =
                sentence.as<double>(COURSE_OVER_GROUND) * boost::units::degree::degree;
        }

        if ((!sentence.at(MAGNETIC_VARIATION).empty()) && (!sentence.at(MAG_VARIATION_EW).empty()))
        {
            double sign = 1;
            if (sentence.as<char>(MAG_VARIATION_EW) == 'W')
                sign = -1;

            magnetic_variation =
                sign * sentence.as<double>(MAGNETIC_VARIATION) * boost::units::degree::degree;
        }
    }
}

goby::util::NMEASentence goby::util::gps::RMC::serialize(std::string talker_id,
                                                         int num_fields) const
{
    goby::util::NMEASentence nmea("$" + talker_id + "RMC", goby::util::NMEASentence::IGNORE);
    nmea.resize(num_fields);

    if (time)
    {
        auto pt = goby::time::convert<boost::posix_time::ptime>(*time);
        if (!pt.is_special())
        {
            nmea[UTC_TIME] = boost::str(boost::format("%02i%02i%02i") % pt.time_of_day().hours() %
                                        pt.time_of_day().minutes() % pt.time_of_day().seconds());
            nmea[DATE] =
                boost::str(boost::format("%02i%02i%02i") % static_cast<int>(pt.date().day()) %
                           static_cast<int>(pt.date().month()) %
                           (pt.date().year() % 100) // Mod 100 to get 2 digit year
                );
        }
    }

    if (status)
        nmea[VALIDITY] = (*status == DataValid) ? "A" : "V";

    if (latitude)
        std::tie(nmea[LATITUDE], nmea[LATITUDE_NS]) = decimal_to_nmea_geo(latitude->value(), LAT);

    if (longitude)
        std::tie(nmea[LONGITUDE], nmea[LONGITUDE_EW]) =
            decimal_to_nmea_geo(longitude->value(), LON);

    if (speed_over_ground)
    {
        using knots = boost::units::metric::knot_base_unit::unit_type;
        boost::units::quantity<knots> sog_as_knots(*speed_over_ground);
        nmea[SPEED_OVER_GROUND] = boost::str(boost::format("%.1f") % sog_as_knots.value());
    }

    if (course_over_ground)
    {
        nmea[COURSE_OVER_GROUND] = boost::str(boost::format("%.1f") % course_over_ground->value());
    }

    if (magnetic_variation)
    {
        nmea[MAGNETIC_VARIATION] =
            boost::str(boost::format("%3.1f") % std::abs(magnetic_variation->value()));

        nmea[MAG_VARIATION_EW] = magnetic_variation->value() >= 0.0 ? "E" : "W";
    }

    return nmea;
}

void goby::util::gps::HDT::parse(const NMEASentence& sentence)
{
    if (sentence.size() >= min_size)
    {
        if (!sentence.at(HEADING).empty())
            true_heading = sentence.as<double>(HEADING) * boost::units::degree::degree;
    }
}

goby::util::NMEASentence goby::util::gps::HDT::serialize(std::string talker_id) const
{
    goby::util::NMEASentence nmea("$" + talker_id + "HDT", goby::util::NMEASentence::IGNORE);
    nmea.resize(size);
    nmea[T] = "T";
    if (true_heading)
    {
        boost::units::quantity<boost::units::degree::plane_angle> wrapped_heading = *true_heading;
        decltype(wrapped_heading) one_rev = 360.0 * boost::units::degree::degrees;
        while (wrapped_heading < 0.0 * boost::units::degree::degrees) wrapped_heading += one_rev;
        while (wrapped_heading >= one_rev) wrapped_heading -= one_rev;

        nmea[HEADING] = boost::str(boost::format("%3.4f") % wrapped_heading.value());
    }

    return nmea;
}
