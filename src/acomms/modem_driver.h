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

// courtesy header for the acoustic modem driver (libmodemdriver)
#ifndef MODEMDRIVERCOURTESY20091211H
#define MODEMDRIVERCOURTESY20091211H

// driver base class
#include "goby/acomms/modemdriver/driver_base.h"

// WHOI Micro-Modem driver
#include "goby/acomms/modemdriver/mm_driver.h"

// toy example "ABC" driver
#include "goby/acomms/modemdriver/abc_driver.h"

// Benthos ATM-900 driver
#include "goby/acomms/modemdriver/benthos_atm900_driver.h"

// Iridium 9532 driver
#include "goby/acomms/modemdriver/iridium_driver.h"

// Iridium shore-side RUDICS/SBD driver
#include "goby/acomms/modemdriver/iridium_shore_driver.h"

// User Datagram Protocol (UDP) driver
#include "goby/acomms/modemdriver/udp_driver.h"

#endif
