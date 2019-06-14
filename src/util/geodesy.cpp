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

#include <cmath>
#include <iostream>
#include <sstream>

#include <boost/units/io.hpp>
#include <boost/units/systems/si/plane_angle.hpp>

#include "goby/exception.h"

#include "geodesy.h"

goby::util::UTMGeodesy::UTMGeodesy(LatLonPoint origin)
    : origin_geo_(origin), origin_zone_(0), pj_utm_(0), pj_latlong_(0)
{
    double origin_lon_deg = origin.lon / boost::units::degree::degrees;
    origin_zone_ = (static_cast<int>(std::floor((origin_lon_deg + 180) / 6)) + 1) % 60;

    std::stringstream proj_utm;
    proj_utm << "+proj=utm +ellps=WGS84 +zone=" << origin_zone_;

    if (!(pj_utm_ = pj_init_plus(proj_utm.str().c_str())))
        throw(goby::Exception("Failed to initiate utm proj"));
    if (!(pj_latlong_ = pj_init_plus("+proj=latlong +ellps=WGS84")))
        throw(goby::Exception("Failed to initiate latlong proj"));

    // proj.4 requires lat/lon in radians
    double x = boost::units::quantity<boost::units::si::plane_angle>(origin.lon) /
               boost::units::si::radians;
    double y = boost::units::quantity<boost::units::si::plane_angle>(origin.lat) /
               boost::units::si::radians;

    int err;
    if ((err = pj_transform(pj_latlong_, pj_utm_, 1, 1, &x, &y, NULL)))
        throw(
            goby::Exception(std::string("Failed to transform datum, reason: ") + pj_strerrno(err)));

    // output of proj.4 utm conversion is meters
    origin_utm_.x = x * boost::units::si::meters;
    origin_utm_.y = y * boost::units::si::meters;
}

goby::util::UTMGeodesy::~UTMGeodesy()
{
    pj_free(pj_utm_);
    pj_free(pj_latlong_);
}

goby::util::UTMGeodesy::XYPoint goby::util::UTMGeodesy::convert(LatLonPoint geo)
{
    double x =
        boost::units::quantity<boost::units::si::plane_angle>(geo.lon) / boost::units::si::radians;
    double y =
        boost::units::quantity<boost::units::si::plane_angle>(geo.lat) / boost::units::si::radians;

    int err;
    if ((err = pj_transform(pj_latlong_, pj_utm_, 1, 1, &x, &y, NULL)))
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
}

goby::util::UTMGeodesy::LatLonPoint goby::util::UTMGeodesy::convert(XYPoint utm)
{
    double lon = (utm.x + origin_utm_.x) / boost::units::si::meters;
    double lat = (utm.y + origin_utm_.y) / boost::units::si::meters;

    int err;
    if ((err = pj_transform(pj_utm_, pj_latlong_, 1, 1, &lon, &lat, NULL)))
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
}
