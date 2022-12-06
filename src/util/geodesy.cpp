// Copyright 2018-2022:
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

#include <cmath>    // for floor
#include <iostream> // for operator<<, basic_...
#include <string>   // for operator+, basic_s...

#include <boost/units/io.hpp>                     // for operator<<
#include <boost/units/systems/si/plane_angle.hpp> // for plane_angle, radians
#include <boost/units/unit.hpp>                   // for unit

#include "goby/exception.h" // for Exception

#include "geodesy.h"

#ifndef USE_PROJ4
#include <proj.h> // proj6+

// use proj4 API for Proj5
#if PROJ_VERSION_MAJOR < 6
#define USE_PROJ4
#endif

#endif

#ifdef USE_PROJ4
#define ACCEPT_USE_OF_DEPRECATED_PROJ_API_H
#include <proj_api.h> // proj4
#endif

goby::util::UTMGeodesy::UTMGeodesy(const LatLonPoint& origin)
    : origin_geo_(origin), origin_zone_(0), pj4_utm_(nullptr), pj4_latlong_(nullptr), pj6_(nullptr)
{
    // avoid -Wunused-private-field by assigning here
    pj4_utm_ = nullptr;
    pj4_latlong_ = nullptr;
    pj6_ = nullptr;

    double origin_lon_deg = origin.lon / boost::units::degree::degrees;
    origin_zone_ = (static_cast<int>(std::floor((origin_lon_deg + 180) / 6))) % 60 + 1;

    std::stringstream proj_utm, proj_latlong;
    proj_utm << "+proj=utm +ellps=WGS84 +zone=" << origin_zone_;
    proj_latlong << "+proj=latlong +ellps=WGS84";

#ifdef USE_PROJ4
    if (!(pj4_utm_ = pj_init_plus(proj_utm.str().c_str())))
        throw(goby::Exception("Failed to initiate utm proj"));
    if (!(pj4_latlong_ = pj_init_plus(proj_latlong.str().c_str())))
        throw(goby::Exception("Failed to initiate latlong proj"));

    // proj.4 requires lat/lon in radians
    double x = boost::units::quantity<boost::units::si::plane_angle>(origin.lon) /
               boost::units::si::radians;
    double y = boost::units::quantity<boost::units::si::plane_angle>(origin.lat) /
               boost::units::si::radians;

    int err;
    if ((err = pj_transform(static_cast<projPJ>(pj4_latlong_), static_cast<projPJ>(pj4_utm_), 1, 1,
                            &x, &y, nullptr)))
        throw(
            goby::Exception(std::string("Failed to transform datum, reason: ") + pj_strerrno(err)));

    // output of proj.4 utm conversion is meters
    origin_utm_.x = x * boost::units::si::meters;
    origin_utm_.y = y * boost::units::si::meters;
#else
    PJ_COORD c, c_out;

    pj6_ = proj_create_crs_to_crs(PJ_DEFAULT_CTX, proj_latlong.str().c_str(),
                                  proj_utm.str().c_str(), NULL);

    if (!pj6_)
        throw(goby::Exception("Failed to create PJ object for projection transformation"));

    c.lpzt.lam = boost::units::quantity<boost::units::degree::plane_angle>(origin.lon) /
                 boost::units::degree::degrees;
    c.lpzt.phi = boost::units::quantity<boost::units::degree::plane_angle>(origin.lat) /
                 boost::units::degree::degrees;
    c.lpzt.z = 0.0;
    c.lpzt.t = HUGE_VAL;

    c_out = proj_trans(static_cast<PJ*>(pj6_), PJ_FWD, c);
    origin_utm_.x = c_out.xy.x * boost::units::si::meters;
    origin_utm_.y = c_out.xy.y * boost::units::si::meters;
#endif
}

goby::util::UTMGeodesy::~UTMGeodesy()
{
#ifdef USE_PROJ4
    pj_free(static_cast<projPJ>(pj4_utm_));
    pj_free(static_cast<projPJ>(pj4_latlong_));
#else
    proj_destroy(static_cast<PJ*>(pj6_));
#endif
}

goby::util::UTMGeodesy::XYPoint goby::util::UTMGeodesy::convert(const LatLonPoint& geo) const
{
#ifdef USE_PROJ4
    double x =
        boost::units::quantity<boost::units::si::plane_angle>(geo.lon) / boost::units::si::radians;
    double y =
        boost::units::quantity<boost::units::si::plane_angle>(geo.lat) / boost::units::si::radians;

    int err;
    if ((err = pj_transform(static_cast<projPJ>(pj4_latlong_), static_cast<projPJ>(pj4_utm_), 1, 1,
                            &x, &y, nullptr)))
    {
        std::stringstream err_ss;
        err_ss << "Failed to transform (lat,lon) = (" << geo.lat << "," << geo.lon
               << "), reason: " << pj_strerrno(err);
        throw(goby::Exception(err_ss.str()));
    }

    XYPoint utm;
    utm.x = x * boost::units::si::meters - origin_utm_.x;
    utm.y = y * boost::units::si::meters - origin_utm_.y;
    return utm;
#else
    PJ_COORD c, c_out;

    c.lpzt.lam = boost::units::quantity<boost::units::degree::plane_angle>(geo.lon) /
                 boost::units::degree::degrees;
    c.lpzt.phi = boost::units::quantity<boost::units::degree::plane_angle>(geo.lat) /
                 boost::units::degree::degrees;
    c.lpzt.z = 0.0;
    c.lpzt.t = HUGE_VAL;

    c_out = proj_trans(static_cast<PJ*>(pj6_), PJ_FWD, c);

    XYPoint utm;
    utm.x = c_out.xy.x * boost::units::si::meters - origin_utm_.x;
    utm.y = c_out.xy.y * boost::units::si::meters - origin_utm_.y;
    return utm;
#endif
}

goby::util::UTMGeodesy::LatLonPoint goby::util::UTMGeodesy::convert(const XYPoint& utm) const
{
#ifdef USE_PROJ4
    double lon = (utm.x + origin_utm_.x) / boost::units::si::meters;
    double lat = (utm.y + origin_utm_.y) / boost::units::si::meters;

    int err;
    if ((err = pj_transform(static_cast<projPJ>(pj4_utm_), static_cast<projPJ>(pj4_latlong_), 1, 1,
                            &lon, &lat, nullptr)))
    {
        std::stringstream err_ss;
        err_ss << "Failed to transform (x,y) = (" << utm.x << "," << utm.y
               << "), reason: " << pj_strerrno(err);
        throw(goby::Exception(err_ss.str()));
    }

    LatLonPoint geo;
    geo.lon =
        boost::units::quantity<boost::units::degree::plane_angle>(lon * boost::units::si::radians);
    geo.lat =
        boost::units::quantity<boost::units::degree::plane_angle>(lat * boost::units::si::radians);
    return geo;
#else

    PJ_COORD c, c_out;

    c.xyzt.x = (utm.x + origin_utm_.x) / boost::units::si::meters;
    c.xyzt.y = (utm.y + origin_utm_.y) / boost::units::si::meters;
    c.xyzt.z = 0.0;
    c.xyzt.t = HUGE_VAL;

    c_out = proj_trans(static_cast<PJ*>(pj6_), PJ_INV, c);

    LatLonPoint geo;
    geo.lon = boost::units::quantity<boost::units::degree::plane_angle>(
        c_out.lpzt.lam * boost::units::degree::degrees);
    geo.lat = boost::units::quantity<boost::units::degree::plane_angle>(
        c_out.lpzt.phi * boost::units::degree::degrees);
    return geo;
#endif
}
