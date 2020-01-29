// Copyright 2009-2018 Toby Schneider (http://gobysoft.org/index.wt/people/toby)
//                     GobySoft, LLC (2013-)
//                     Massachusetts Institute of Technology (2007-2014)
//
//
// This file is part of the Goby Underwater Autonomy Project Binaries
// ("The Goby Binaries").
//
// The Goby Binaries are free software: you can redistribute them and/or modify
// them under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 2 of the License, or
// (at your option) any later version.
//
// The Goby Binaries are distributed in the hope that they will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with Goby.  If not, see <http://www.gnu.org/licenses/>.

#include "mavlink.h"

std::unordered_map<uint32_t, mavlink::mavlink_msg_entry_t>
    goby::middleware::MAVLinkRegistry::entries_;
std::mutex goby::middleware::MAVLinkRegistry::mavlink_registry_mutex_;

void goby::middleware::MAVLinkRegistry::register_default_dialects()
{
    register_dialect_entries(mavlink::common::MESSAGE_ENTRIES);
}

namespace mavlink
{
const mavlink_msg_entry_t* mavlink_get_msg_entry(uint32_t msgid);
}

const mavlink::mavlink_msg_entry_t* mavlink::mavlink_get_msg_entry(uint32_t msgid)
{
    return goby::middleware::MAVLinkRegistry::get_msg_entry(msgid);
}
