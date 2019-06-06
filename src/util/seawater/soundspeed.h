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

#ifndef SOUNDSPEED_20190606H
#define SOUNDSPEED_20190606H

namespace goby
{
namespace util
{
/// K.V. Mackenzie, Nine-term equation for the sound speed in the oceans (1981) J. Acoust. Soc. Am. 70(3), pp 807-812
/// http://scitation.aip.org/getabs/servlet/GetabsServlet?prog=normal&id=JASMAN000070000003000807000001&idtype=cvips&gifs=yes
/// \param T temperature in degrees Celcius (see paper for applicable ranges)
/// \param S salinity (unitless, calculated using Practical Salinity Scale) (see paper for applicable ranges)
/// \param D depth in meters (see paper for applicable ranges)
/// \return speed of sound in meters per second
inline double mackenzie_soundspeed(double T, double S, double D)
{
    return 1448.96 + 4.591 * T - 5.304e-2 * T * T + 2.374e-4 * T * T * T + 1.340 * (S - 35) +
           1.630e-2 * D + 1.675e-7 * D * D - 1.025e-2 * T * (S - 35) - 7.139e-13 * T * D * D * D;
}

} // namespace util
} // namespace goby

#endif
