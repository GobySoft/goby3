// Copyright 2020:
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

// fixes linker problem with Boost 1.71 and Clang 10 with -g enabled:
// /usr/bin/ld: ../../../../lib/libgoby.so.3.0.0~beta10+11+g47dd90bc-dirty: undefined reference to `boost::statechart::detail::no_context<goby::acomms::benthos::fsm::EvReset>::no_function(goby::acomms::benthos::fsm::EvReset const&)'

#if __clang_major__ == 10
#ifndef GOBY_WORKAROUND_BOOST_STATECHART_NO_FUNCTION
#define GOBY_WORKAROUND_BOOST_STATECHART_NO_FUNCTION
template <class Event>
void boost::statechart::detail::no_context<Event>::no_function(const Event&){};
#endif
#endif
