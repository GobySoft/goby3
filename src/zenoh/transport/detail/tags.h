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

#ifndef GOBY_ZENOH_TRANSPORT_DETAIL_TAGS_H
#define GOBY_ZENOH_TRANSPORT_DETAIL_TAGS_H

namespace goby
{
namespace zenoh
{
namespace detail
{

/// \brief ImplementationTag for zenoh interprocess transporters
struct InterProcessTag
{
    inline static constexpr const char prefix[] = "goby::zenoh::interprocess";
    /// \brief Key expression chunk that separates this layer's traffic from the other layers sharing the Zenoh session
    inline static constexpr const char layer[] = "interprocess";
};

/// \brief ImplementationTag for zenoh intermodule transporters
struct InterModuleTag
{
    inline static constexpr const char prefix[] = "goby::zenoh::intermodule";
    /// \brief Key expression chunk that separates this layer's traffic from the other layers sharing the Zenoh session
    inline static constexpr const char layer[] = "intermodule";
};

} // namespace detail
} // namespace zenoh
} // namespace goby

#endif
