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

#include "goby/util/geodesy.h"
#include <cassert>
#include <iomanip>
#include <iostream>

#include <boost/units/io.hpp>

using namespace goby::util;

bool double_cmp(double a, double b, int precision)
{
    return std::abs(a - b) < pow(10.0, -precision);
}

int main()
{
    using boost::units::degree::degrees;
    using boost::units::si::meters;

    {
        goby::util::UTMGeodesy geodesy(
            {42.177127968804754 * degrees, -70.16303866815588 * degrees});
        std::cout << "zone: " << geodesy.origin_utm_zone() << std::endl;
        assert(geodesy.origin_utm_zone() == 19);

        auto origin_utm = geodesy.origin_utm();

        std::cout << "utm origin: " << std::setprecision(std::numeric_limits<double>::digits10)
                  << origin_utm.x << ", " << origin_utm.y << std::endl;

        assert(double_cmp(origin_utm.x / meters, 403946.82376733015, 3));
        assert(double_cmp(origin_utm.y / meters, 4670097.454234971, 3));
    }

    {
        goby::util::UTMGeodesy geodesy({41 * degrees, -70 * degrees});

        auto geo = geodesy.convert(goby::util::UTMGeodesy::XYPoint({100 * meters, 100 * meters}));
        auto origin_geo = geodesy.origin_geo();

        std::cout << "geo origin: " << std::setprecision(std::numeric_limits<double>::digits10)
                  << origin_geo.lat << ", " << origin_geo.lon << std::endl;
        std::cout << "(x = 100, y = 100) as (lat, lon): ("
                  << std::setprecision(std::numeric_limits<double>::digits10) << geo.lat << ", "
                  << geo.lon << ")" << std::endl;

        assert(double_cmp(geo.lat / degrees, 41.00091, 5));
        assert(double_cmp(geo.lon / degrees, -69.99882, 5));

        auto utm = geodesy.convert(geo);
        std::cout << "reconvert as (x, y): ("
                  << std::setprecision(std::numeric_limits<double>::digits10) << utm.x << ", "
                  << utm.y << ")" << std::endl;
        assert(double_cmp(utm.x / meters, 100, 3));
        assert(double_cmp(utm.y / meters, 100, 3));
    }

    std::cout << "all tests passed" << std::endl;
    return 0;
}
