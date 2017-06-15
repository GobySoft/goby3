// Copyright 2009-2017 Toby Schneider (http://gobysoft.org/index.wt/people/toby)
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

// This file uses the same basic API provided by MOOS pre-v10
// (C) Paul Newman, released GPL v3

#ifndef MOOSGeodesy20130916H
#define MOOSGeodesy20130916H

#include <proj_api.h>

class CMOOSGeodesy
{
public:
    CMOOSGeodesy();
    virtual ~CMOOSGeodesy();
    
    double GetOriginLatitude();
    double GetOriginLongitude();
    double GetOriginNorthing();
    double GetOriginEasting();
    int GetUTMZone();
    
    bool LatLong2LocalUTM(double lat, double lon, double & MetersNorth, double & MetersEast);
    bool UTM2LatLong(double dfX, double dfY, double& dfLat, double& dfLong);    
    
    bool Initialise(double lat, double lon);

private:
    int m_sUTMZone;
    double m_dOriginEasting;
    double m_dOriginNorthing;
    double m_dOriginLongitude;
    double m_dOriginLatitude;
    projPJ pj_utm_, pj_latlong_;
    
    void SetOriginLatitude(double lat);
    void SetOriginLongitude(double lon);
    void SetOriginEasting(double East);
    void SetOriginNorthing(double North);
    void SetUTMZone(int zone);

};

#endif
