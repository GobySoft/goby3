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

#ifndef SCI20100713H
#define SCI20100713H

#include <cmath>
#include <map>

namespace goby
{
namespace util
{
/// \name Science
//@{

/// round 'r' to 'dec' number of decimal places
/// we want no upward bias so
/// round 5 up if odd next to it, down if even
/// \param r value to round
/// \param dec number of places past the decimal to round (e.g. dec=1 rounds to tenths)
/// \return r rounded
[[deprecated("use std::round() or dccl::round()")]] inline double unbiased_round(double r,
                                                                                 double dec)
{
    double ex = std::pow(10.0, dec);
    double final = std::floor(r * ex);
    double s = (r * ex) - final;

    // remainder less than 0.5 or even number next to it
    if (s < 0.5 || (s == 0.5 && !(static_cast<unsigned long>(final) & 1)))
        return final / ex;
    else
        return (final + 1) / ex;
}

/// \return ceil(log2(v))
inline unsigned ceil_log2(unsigned v)
{
    // r will be one greater (ceil) if v is not a power of 2
    unsigned r = ((v & (v - 1)) == 0) ? 0 : 1;
    while (v >>= 1) r++;
    return r;
}

inline unsigned ceil_log2(double d) { return ceil_log2(static_cast<unsigned>(std::ceil(d))); }

inline unsigned ceil_log2(int i) { return ceil_log2(static_cast<unsigned>(i)); }

[[deprecated("use std::log2()")]] inline double log2(double d) { return std::log2(d); }

/// \brief Linear interpolation function
///
/// \param a Value to interpolate
/// \param table Table of values to interpolate from
/// \return Interpolated value
template <typename N1, typename N2> N2 linear_interpolate(N1 a, const std::map<N1, N2> table)
{
    auto l_it = table.upper_bound(a);

    // clip to max value
    if (l_it == table.end())
    {
        return (--l_it)->second;
    }
    // clip to min value
    else if (l_it == table.begin())
    {
        return l_it->second;
    }
    // linear interpolation
    else
    {
        auto u_it = l_it;
        --l_it;
        auto a_u = u_it->first, a_l = l_it->first;
        auto b_u = u_it->second, b_l = l_it->second;
        return ((a - a_l) / (a_l - a_u)) * (b_l - b_u) + b_l;
    }
}

} // namespace util

//@}

} // namespace goby

#endif
