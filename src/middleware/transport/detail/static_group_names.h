// Copyright 2026:
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

#ifndef GOBY_MIDDLEWARE_TRANSPORT_DETAIL_STATIC_GROUP_NAMES_H
#define GOBY_MIDDLEWARE_TRANSPORT_DETAIL_STATIC_GROUP_NAMES_H

#include <array>

namespace goby::middleware::detail
{

// helper for concatenating prefix to group name
template <std::size_t N1, std::size_t N2>
constexpr auto concat(const char (&a)[N1], const char (&b)[N2])
{
    std::array<char, N1 + N2 - 1> out{};
    for (std::size_t i = 0; i < N1 - 1; ++i) out[i] = a[i];
    for (std::size_t i = 0; i < N2; ++i) out[i + N1 - 1] = b[i];
    return out;
}

} // namespace goby::middleware::detail

#endif
