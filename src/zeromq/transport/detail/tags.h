// Copyright 2020-2026:
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

#ifndef GOBY_ZEROMQ_TRANSPORT_DETAIL_TAGS_H
#define GOBY_ZEROMQ_TRANSPORT_DETAIL_TAGS_H

namespace goby
{
namespace zeromq
{
namespace detail
{

/// \brief ImplementationTag for zeromq interprocess transporters
struct InterProcessTag
{
    inline static constexpr const char prefix[] = "goby::zeromq::interprocess";
};

/// \brief ImplementationTag for zeromq intermodule transporters
struct InterModuleTag
{
    inline static constexpr const char prefix[] = "goby::zeromq::intermodule";
};

} // namespace detail
} // namespace zeromq
} // namespace goby

#endif
