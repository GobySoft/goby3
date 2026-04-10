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

#ifndef GOBY_UDPM_TRANSPORT_INTERMODULE_H
#define GOBY_UDPM_TRANSPORT_INTERMODULE_H

#include "goby/middleware/transport/intermodule.h"

#include "goby/udpm/transport/detail/tags.h"
#include "goby/udpm/transport/interprocess.h"

namespace goby
{
namespace udpm
{
template <typename InnerTransporter = middleware::NullTransporter>
using InterModulePortal =
    InterProcessPortalImplementation<InnerTransporter, middleware::InterModulePortalBase,
                                     detail::InterModuleTag>;

template <typename InnerTransporter = middleware::NullTransporter>
using InterModuleForwarder =
    middleware::InterModuleForwarder<InnerTransporter, detail::InterModuleTag>;

} // namespace udpm
} // namespace goby

#endif
