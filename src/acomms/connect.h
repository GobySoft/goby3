// Copyright 2011-2020:
//   GobySoft, LLC (2013-)
//   Massachusetts Institute of Technology (2007-2014)
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

// gives functions for connecting the slots and signals of the goby-acomms libraries together

#ifndef CONNECT20110121H
#define CONNECT20110121H

#include <boost/bind.hpp>
#include <boost/signals2.hpp>

namespace goby
{
namespace acomms
{
/// connect a signal to a slot (e.g. function pointer)
template <typename Signal, typename Slot> void connect(Signal* signal, Slot slot)
{
    signal->connect(slot);
}

/// connect a signal to a member function with one argument
template <typename Signal, typename Obj, typename A1>
void connect(Signal* signal, Obj* obj, void (Obj::*mem_func)(A1))
{
    connect(signal, boost::bind(mem_func, obj, _1));
}

/// connect a signal to a member function with two arguments
template <typename Signal, typename Obj, typename A1, typename A2>
void connect(Signal* signal, Obj* obj, void (Obj::*mem_func)(A1, A2))
{
    connect(signal, boost::bind(mem_func, obj, _1, _2));
}

/// connect a signal to a member function with three arguments
template <typename Signal, typename Obj, typename A1, typename A2, typename A3>
void connect(Signal* signal, Obj* obj, void (Obj::*mem_func)(A1, A2, A3))
{
    connect(signal, boost::bind(mem_func, obj, _1, _2, _3));
}

/// disconnect a signal to a slot (e.g. function pointer)
template <typename Signal, typename Slot> void disconnect(Signal* signal, Slot slot)
{
    signal->disconnect(slot);
}

/// disconnect a signal to a member function with one argument
template <typename Signal, typename Obj, typename A1>
void disconnect(Signal* signal, Obj* obj, void (Obj::*mem_func)(A1))
{
    disconnect(signal, boost::bind(mem_func, obj, _1));
}

/// disconnect a signal to a member function with two arguments
template <typename Signal, typename Obj, typename A1, typename A2>
void disconnect(Signal* signal, Obj* obj, void (Obj::*mem_func)(A1, A2))
{
    disconnect(signal, boost::bind(mem_func, obj, _1, _2));
}

/// disconnect a signal to a member function with three arguments
template <typename Signal, typename Obj, typename A1, typename A2, typename A3>
void disconnect(Signal* signal, Obj* obj, void (Obj::*mem_func)(A1, A2, A3))
{
    disconnect(signal, boost::bind(mem_func, obj, _1, _2, _3));
}

} // namespace acomms
} // namespace goby

#endif
