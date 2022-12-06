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

#ifndef GOBY_UTIL_GEODESY_H
#define GOBY_UTIL_GEODESY_H

#include <boost/units/quantity.hpp>              // for quantity
#include <boost/units/systems/angle/degrees.hpp> // for plane_angle
#include <boost/units/systems/si/length.hpp>     // for length


namespace goby
{
namespace util
{
class UTMGeodesy
{
  public:
    struct LatLonPoint
    {
        boost::units::quantity<boost::units::degree::plane_angle> lat;
        boost::units::quantity<boost::units::degree::plane_angle> lon;
    };

    struct XYPoint
    {
        boost::units::quantity<boost::units::si::length> x;
        boost::units::quantity<boost::units::si::length> y;
    };

    UTMGeodesy(const LatLonPoint& origin);
    virtual ~UTMGeodesy();

    LatLonPoint origin_geo() const { return origin_geo_; }
    XYPoint origin_utm() const { return origin_utm_; }
    int origin_utm_zone() const { return origin_zone_; }

    LatLonPoint convert(const XYPoint& utm) const;
    XYPoint convert(const LatLonPoint& geo) const;

  private:
    LatLonPoint origin_geo_;
    int origin_zone_;
    XYPoint origin_utm_;

    // proj4
    void *pj4_utm_, *pj4_latlong_;

    // proj6+
    void *pj6_;
};
} // namespace util
} // namespace goby

#endif
