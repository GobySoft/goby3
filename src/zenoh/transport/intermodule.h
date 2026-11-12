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

#ifndef GOBY_ZENOH_TRANSPORT_INTERMODULE_H
#define GOBY_ZENOH_TRANSPORT_INTERMODULE_H

#include "goby/middleware/transport/detail/implementation_traits.h"
#include "goby/middleware/transport/intermodule.h"

#include "goby/zenoh/transport/detail/tags.h"
#include "goby/zenoh/transport/interprocess.h"

namespace goby
{
namespace zenoh
{
template <typename InnerTransporter = middleware::NullTransporter>
using InterModulePortal =
    InterProcessPortalImplementation<InnerTransporter, middleware::InterModulePortalBase,
                                     detail::InterModuleTag>;

template <typename InnerTransporter = middleware::NullTransporter>
using InterModuleForwarder =
    middleware::InterModuleForwarder<InnerTransporter, detail::InterModuleTag>;

} // namespace zenoh
} // namespace goby

namespace goby
{
namespace middleware
{
namespace detail
{
template <> struct implementation_traits<goby::zenoh::detail::InterModuleTag>
{
    template <typename InnerTransporter>
    using Portal = goby::zenoh::InterModulePortal<InnerTransporter>;
    using PortalConfig = goby::zenoh::protobuf::InterProcessPortalConfig;
    static constexpr const char* name = "zenoh";
};
} // namespace detail
} // namespace middleware
} // namespace goby

#endif
