// Copyright 2013-2019:
//   GobySoft, LLC (2013-)
//   Massachusetts Institute of Technology (2007-2014)
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

#include <iostream>

#include "goby/exception.h"
#include "goby/util/geodesy.h"

#include "moos_geodesy.h"

goby::moos::CMOOSGeodesy::CMOOSGeodesy() {}
goby::moos::CMOOSGeodesy::~CMOOSGeodesy() {}

bool goby::moos::CMOOSGeodesy::Initialise(double lat, double lon)
{
    try
    {
        geodesy_.reset(new goby::util::UTMGeodesy(
            {lat * boost::units::degree::degrees, lon * boost::units::degree::degrees}));
    }
    catch (goby::Exception& e)
    {
        std::cerr << e.what();
        return false;
    }
    return true;
}

double goby::moos::CMOOSGeodesy::GetOriginLongitude()
{
    if (!geodesy_)
        return std::numeric_limits<double>::quiet_NaN();
    else
        return geodesy_->origin_geo().lon / boost::units::degree::degrees;
}

double goby::moos::CMOOSGeodesy::GetOriginLatitude()
{
    if (!geodesy_)
        return std::numeric_limits<double>::quiet_NaN();
    else
        return geodesy_->origin_geo().lat / boost::units::degree::degrees;
}

int goby::moos::CMOOSGeodesy::GetUTMZone()
{
    if (!geodesy_)
        return -1;
    else
        return geodesy_->origin_utm_zone();
}

bool goby::moos::CMOOSGeodesy::LatLong2LocalUTM(double lat, double lon, double& MetersNorth,
                                                double& MetersEast)
{
    if (!geodesy_)
    {
        std::cerr << "Must call Initialise before calling LatLong2LocalUTM" << std::endl;
        return false;
    }

    MetersNorth = std::numeric_limits<double>::quiet_NaN();
    MetersEast = std::numeric_limits<double>::quiet_NaN();

    try
    {
        auto xy = geodesy_->convert(
            {lat * boost::units::degree::degrees, lon * boost::units::degree::degrees});
        MetersNorth = xy.y / boost::units::si::meters;
        MetersEast = xy.x / boost::units::si::meters;
    }
    catch (goby::Exception& e)
    {
        std::cerr << e.what();
        return false;
    }
    return true;
}

double goby::moos::CMOOSGeodesy::GetOriginEasting()
{
    if (!geodesy_)
        return std::numeric_limits<double>::quiet_NaN();
    else
        return geodesy_->origin_utm().x / boost::units::si::meters;
}

double goby::moos::CMOOSGeodesy::GetOriginNorthing()
{
    if (!geodesy_)
        return std::numeric_limits<double>::quiet_NaN();
    else
        return geodesy_->origin_utm().y / boost::units::si::meters;
}

bool goby::moos::CMOOSGeodesy::UTM2LatLong(double dfX, double dfY, double& dfLat, double& dfLong)
{
    if (!geodesy_)
    {
        std::cerr << "Must call Initialise before calling UTM2LatLong" << std::endl;
        return false;
    }

    dfLat = std::numeric_limits<double>::quiet_NaN();
    dfLong = std::numeric_limits<double>::quiet_NaN();

    try
    {
        auto latlon =
            geodesy_->convert({dfX * boost::units::si::meters, dfY * boost::units::si::meters});
        dfLat = latlon.lat / boost::units::degree::degrees;
        dfLong = latlon.lon / boost::units::degree::degrees;
    }
    catch (goby::Exception& e)
    {
        std::cerr << e.what();
        return false;
    }
    return true;
}
