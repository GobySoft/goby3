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

#ifndef GobyGeodesy20180312H
#define GobyGeodesy20180312H

#include <limits>

#include <boost/units/quantity.hpp>
#include <boost/units/systems/angle/degrees.hpp>
#include <boost/units/systems/si/length.hpp>

#include <proj_api.h>

#include "goby/util/sci.h"

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

    UTMGeodesy(LatLonPoint origin);
    virtual ~UTMGeodesy();

    LatLonPoint origin_geo() const { return origin_geo_; }
    XYPoint origin_utm() const { return origin_utm_; }
    int origin_utm_zone() const { return origin_zone_; }

    LatLonPoint convert(XYPoint utm) const;
    XYPoint convert(LatLonPoint geo) const;

  private:
    LatLonPoint origin_geo_;
    int origin_zone_;
    XYPoint origin_utm_;
    projPJ pj_utm_, pj_latlong_;
};
} // namespace util
} // namespace goby

#endif
