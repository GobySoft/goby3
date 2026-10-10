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

#ifndef GOBY_MIDDLEWARE_TRANSPORT_DETAIL_IMPLEMENTATION_TRAITS_H
#define GOBY_MIDDLEWARE_TRANSPORT_DETAIL_IMPLEMENTATION_TRAITS_H

namespace goby
{
namespace middleware
{
namespace detail
{
/// \brief Maps an ImplementationTag to the portal that implements it, so that generic code can be written against a tag alone.
///
/// Each implementation specializes this alongside its portal
/// (e.g. goby/zeromq/transport/interprocess.h), providing:
///
/// \code
/// template <typename InnerTransporter> using Portal = ...;
/// using PortalConfig = ...;
/// static constexpr const char* name = ...;
/// \endcode
///
/// \c name is the implementation's short name, used for the "goby <name>" command line
/// action and for the "goby::<name>::_internal*" groups reserved by the implementation.
template <typename ImplementationTag> struct implementation_traits;

} // namespace detail
} // namespace middleware
} // namespace goby

#endif
