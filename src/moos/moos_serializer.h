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

#include <string>

#include "goby/moos/moos_header.h"

namespace goby
{
namespace moos
{
class MOOSSerializer
{
  public:
    static void serialize(const CMOOSMsg& const_msg, std::string* data)
    {
        // copy because Serialize wants to modify the CMOOSMsg
        CMOOSMsg msg(const_msg);
        // adapted from MOOSCommPkt.cpp
        int serialized_size = msg.GetSizeInBytesWhenSerialised();
        data->resize(serialized_size);
        serialized_size =
            msg.Serialize(reinterpret_cast<unsigned char*>(&(*data)[0]), serialized_size);
        data->resize(serialized_size);
    }

    static void parse(CMOOSMsg* msg, std::string data)
    {
        msg->Serialize(reinterpret_cast<unsigned char*>(&data[0]), data.size(), false);
    }
};
} // namespace moos
} // namespace goby
