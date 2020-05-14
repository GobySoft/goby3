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

// This file uses the same basic API provided by MOOS pre-v10
// (C) Paul Newman, released GPL v3

#ifndef GobyMOOSGeodesy20130916H
#define GobyMOOSGeodesy20130916H

#include <memory>

namespace goby
{
namespace util
{
class UTMGeodesy;
}
} // namespace goby

namespace goby
{
namespace moos
{
class CMOOSGeodesy
{
  public:
    CMOOSGeodesy();
    ~CMOOSGeodesy();

    double GetOriginLatitude();
    double GetOriginLongitude();

    double GetOriginNorthing();
    double GetOriginEasting();
    int GetUTMZone();

    bool LatLong2LocalUTM(double lat, double lon, double& MetersNorth, double& MetersEast);
    bool UTM2LatLong(double dfX, double dfY, double& dfLat, double& dfLong);

    bool Initialise(double lat, double lon);

  private:
    std::unique_ptr<goby::util::UTMGeodesy> geodesy_;
};
} // namespace moos
} // namespace goby

#endif
