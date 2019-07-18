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

#include "interface.h"

const std::map<int, std::string> goby::middleware::MarshallingScheme::e2s = {
    {CSTR, "CSTR"}, {PROTOBUF, "PROTOBUF"}, {DCCL, "DCCL"}, {MAVLINK, "MAVLINK"}};

std::map<std::string, int> invert_map(const std::map<int, std::string>& e2s)
{
    std::map<std::string, int> s2e;
    for (auto p : e2s) s2e.insert(std::make_pair(p.second, p.first));
    return s2e;
}
const std::map<std::string, int> goby::middleware::MarshallingScheme::s2e(invert_map(e2s));
