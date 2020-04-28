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

#ifndef ASIO20200428H
#define ASIO20200428H

#include <boost/version.hpp>

// manage the switch from ASIO io_service to io_context introduced in Boost 1.66 but functions were not reworked until 1.70
#if BOOST_VERSION < 107000
#include <boost/asio/io_service.hpp>
#define USE_BOOST_IO_SERVICE
#endif

// also typedef new names
#if BOOST_VERSION < 106600
namespace boost
{
namespace asio
{
using io_context = io_service;
} // namespace asio
} // namespace boost
#endif

#endif
