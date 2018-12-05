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
#include <limits>
#include <sstream>

#include "moos_geodesy.h"

CMOOSGeodesy::CMOOSGeodesy()
    : m_sUTMZone(0), m_dOriginEasting(0), m_dOriginNorthing(0), m_dOriginLongitude(0),
      m_dOriginLatitude(0), pj_utm_(0), pj_latlong_(0)
{
}

CMOOSGeodesy::~CMOOSGeodesy()
{
    pj_free(pj_utm_);
    pj_free(pj_latlong_);
}

bool CMOOSGeodesy::Initialise(double lat, double lon)
{
    //Set the Origin of the local Grid Coordinate System
    SetOriginLatitude(lat);
    SetOriginLongitude(lon);

    int zone = (static_cast<int>(std::floor((lon + 180) / 6)) + 1) % 60;

    std::stringstream proj_utm;
    proj_utm << "+proj=utm +ellps=WGS84 +zone=" << zone;

    if (!(pj_utm_ = pj_init_plus(proj_utm.str().c_str())))
    {
        std::cerr << "Failed to initiate utm proj" << std::endl;
        return false;
    }
    if (!(pj_latlong_ = pj_init_plus("+proj=latlong +ellps=WGS84")))
    {
        std::cerr << "Failed to initiate latlong proj" << std::endl;
        return false;
    }

    //Translate the Origin coordinates into Northings and Eastings
    double tempNorth = lat * DEG_TO_RAD, tempEast = lon * DEG_TO_RAD;

    int err;
    if (err = pj_transform(pj_latlong_, pj_utm_, 1, 1, &tempEast, &tempNorth, NULL))
    {
        std::cerr << "Failed to transform datum, reason: " << pj_strerrno(err) << std::endl;
        return false;
    }

    //Then set the Origin for the Northing/Easting coordinate frame
    //Also make a note of the UTM Zone we are operating in
    SetOriginNorthing(tempNorth);
    SetOriginEasting(tempEast);
    SetUTMZone(zone);

    return true;
}

double CMOOSGeodesy::GetOriginLongitude() { return m_dOriginLongitude; }

double CMOOSGeodesy::GetOriginLatitude() { return m_dOriginLatitude; }

void CMOOSGeodesy::SetOriginLongitude(double lon) { m_dOriginLongitude = lon; }

void CMOOSGeodesy::SetOriginLatitude(double lat) { m_dOriginLatitude = lat; }

void CMOOSGeodesy::SetOriginNorthing(double North) { m_dOriginNorthing = North; }

void CMOOSGeodesy::SetOriginEasting(double East) { m_dOriginEasting = East; }

void CMOOSGeodesy::SetUTMZone(int zone) { m_sUTMZone = zone; }

int CMOOSGeodesy::GetUTMZone() { return m_sUTMZone; }

bool CMOOSGeodesy::LatLong2LocalUTM(double lat, double lon, double& MetersNorth, double& MetersEast)
{
    double dN, dE;
    double tmpEast = lon * DEG_TO_RAD;
    double tmpNorth = lat * DEG_TO_RAD;
    MetersNorth = std::numeric_limits<double>::quiet_NaN();
    MetersEast = std::numeric_limits<double>::quiet_NaN();

    if (!pj_latlong_ || !pj_utm_)
    {
        std::cerr << "Must call Initialise before calling LatLong2LocalUTM" << std::endl;
        return false;
    }

    int err;
    if (err = pj_transform(pj_latlong_, pj_utm_, 1, 1, &tmpEast, &tmpNorth, NULL))
    {
        std::cerr << "Failed to transform (lat,lon) = (" << lat << "," << lon
                  << "), reason: " << pj_strerrno(err) << std::endl;
        return false;
    }

    MetersNorth = tmpNorth - GetOriginNorthing();
    MetersEast = tmpEast - GetOriginEasting();
    return true;
}

double CMOOSGeodesy::GetOriginEasting() { return m_dOriginEasting; }

double CMOOSGeodesy::GetOriginNorthing() { return m_dOriginNorthing; }

bool CMOOSGeodesy::UTM2LatLong(double dfX, double dfY, double& dfLat, double& dfLong)
{
    double x = dfX + GetOriginEasting();
    double y = dfY + GetOriginNorthing();

    dfLat = std::numeric_limits<double>::quiet_NaN();
    dfLong = std::numeric_limits<double>::quiet_NaN();

    if (!pj_latlong_ || !pj_utm_)
    {
        std::cerr << "Must call Initialise before calling UTM2LatLong" << std::endl;
        return false;
    }

    int err;
    if (err = pj_transform(pj_utm_, pj_latlong_, 1, 1, &x, &y, NULL))
    {
        std::cerr << "Failed to transform (x,y) = (" << dfX << "," << dfY
                  << "), reason: " << pj_strerrno(err) << std::endl;
        return false;
    }

    dfLat = y * RAD_TO_DEG;
    dfLong = x * RAD_TO_DEG;
    return true;
}
