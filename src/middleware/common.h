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

#ifndef Common20200603H
#define Common20200603H

#include "goby/middleware/protobuf/layer.pb.h"

namespace goby
{
namespace middleware
{
inline std::string to_string(goby::middleware::protobuf::Layer layer)
{
    const int underscore_pos = 5; // underscore position in: "LAYER_"
    std::string name = goby::middleware::protobuf::Layer_Name(layer).substr(underscore_pos + 1);
    boost::to_lower(name);
    return name;
}
} // namespace middleware
} // namespace goby

#endif
