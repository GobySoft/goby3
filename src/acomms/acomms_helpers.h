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

#ifndef AcommsHelpers20110208H
#define AcommsHelpers20110208H

#include <bitset>
#include <limits>
#include <string>

#include <google/protobuf/descriptor.h>

#include "goby/acomms/protobuf/modem_message.pb.h"

namespace goby
{
namespace acomms
{
// provides stream output operator for Google Protocol Buffers Message
inline std::ostream& operator<<(std::ostream& out, const google::protobuf::Message& msg)
{
    return (out << "[[" << msg.GetDescriptor()->name() << "]] " << msg.DebugString());
}

} // namespace acomms
} // namespace goby

#endif
