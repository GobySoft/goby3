// Copyright 2009-2020:
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

#ifndef DriverException20100812H
#define DriverException20100812H

#include "goby/acomms/protobuf/modem_driver_status.pb.h"
#include "goby/exception.h"

namespace goby
{
namespace acomms
{
class ModemDriverException : public goby::Exception
{
  public:
    ModemDriverException(const std::string& s, protobuf::ModemDriverStatus::Status stat)
        : Exception(s), stat_(stat)
    {
    }
    protobuf::ModemDriverStatus::Status status() const { return stat_; }

  private:
    protobuf::ModemDriverStatus::Status stat_;
};
} // namespace acomms
} // namespace goby

#endif
