// Copyright 2022-2024:
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

#ifndef GOBY_ZEROMQ_APPLICATION_SIMPLE_THREAD_H
#define GOBY_ZEROMQ_APPLICATION_SIMPLE_THREAD_H

#include "goby/middleware/application/simple_thread.h"
#include "goby/zeromq/transport/detail/tags.h"

namespace goby
{
namespace zeromq
{
/// \brief Zeromq-backed SimpleThread. Derives from middleware::SimpleThread using InterProcessTag.
///
/// \tparam Config Configuration type
template <typename Config>
using SimpleThread = middleware::SimpleThread<Config, detail::InterProcessTag>;

} // namespace zeromq
} // namespace goby

#endif
