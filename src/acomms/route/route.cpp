// Copyright 2012-2020:
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

#include <ostream> // for basic_ostrea...
#include <string>  // for operator<<

#include <google/protobuf/descriptor.h> // for Descriptor
#include <google/protobuf/message.h>    // for Message

#include "goby/acomms/protobuf/queue.pb.h"              // for QueuedMessag...
#include "goby/acomms/queue/queue_manager.h"            // for QueueManager
#include "goby/util/debug_logger/flex_ostreambuf.h"     // for DEBUG1, logger
#include "goby/util/debug_logger/logger_manipulators.h" // for operator<<

#include "route.h"

using goby::glog;
using namespace goby::util::logger;

void goby::acomms::RouteManager::set_cfg(const protobuf::RouteManagerConfig& cfg)
{
    cfg_ = cfg;
    process_cfg();
}

void goby::acomms::RouteManager::merge_cfg(const protobuf::RouteManagerConfig& cfg)
{
    cfg_.MergeFrom(cfg);
    process_cfg();
}

void goby::acomms::RouteManager::process_cfg()
{
    glog.is(DEBUG1) && glog << group("goby::acomms::route")
                            << "Route is: " << cfg_.route().DebugString() << std::endl;
}

void goby::acomms::RouteManager::handle_in(const protobuf::QueuedMessageMeta& meta,
                                           const google::protobuf::Message& data_msg, int modem_id)
{
    if (meta.dest() == modem_id) // no routing to do ...
        return;

    glog.is(DEBUG1) && glog << group("goby::acomms::route") << "Incoming message "
                            << data_msg.GetDescriptor()->full_name()
                            << ", can we route message to destination: " << meta.dest() << "?"
                            << std::endl;

    int next_hop = find_next_hop(modem_id, meta.dest());

    if (next_hop != -1)
    {
        std::uint32_t subnet = next_hop & cfg_.subnet_mask();
        glog.is(DEBUG1) && glog << group("goby::acomms::route")
                                << "Requeuing to proper subnet: " << subnet << " (" << std::hex
                                << next_hop << " & " << cfg_.subnet_mask() << ")" << std::dec
                                << std::endl;
        if (!subnet_map_.count(subnet))
        {
            glog.is(DEBUG1) && glog << group("goby::acomms::route")
                                    << "No subnet available for this message, ignoring."
                                    << std::endl;
            return;
        }

        subnet_map_[subnet]->push_message(data_msg, &meta);
    }
    else
    {
        glog.is(DEBUG1) && glog << group("goby::acomms::route")
                                << "Destination is not in route, ignoring." << std::endl;
    }
}

int goby::acomms::RouteManager::route_index(int modem_id)
{
    for (int i = 0, n = cfg_.route().hop_size(); i < n; ++i)
    {
        if (cfg_.route().hop(i) == modem_id)
            return i;
    }
    return -1;
}

void goby::acomms::RouteManager::handle_out(protobuf::QueuedMessageMeta* meta,
                                            const google::protobuf::Message& /*data_msg*/,
                                            int modem_id)
{
    glog.is(DEBUG1) && glog << group("goby::acomms::route")
                            << "Trying to route outgoing message to destination: " << meta->dest()
                            << std::endl;

    int next_hop = find_next_hop(modem_id, meta->dest());
    if (next_hop != -1)
    {
        meta->set_dest(next_hop);
        glog.is(DEBUG1) && glog << group("goby::acomms::route")
                                << "Set next hop to: " << meta->dest() << std::endl;
    }
}

int goby::acomms::RouteManager::find_next_hop(int us, int dest)
{
    int next_hop = find_next_route_hop(us, dest);
    if (next_hop == -1 && cfg_.has_default_gateway())
    {
        next_hop = cfg_.default_gateway();
        glog.is(DEBUG1) && glog << group("goby::acomms::route")
                                << "Using default gateway: " << next_hop << std::endl;
    }
    return next_hop;
}

int goby::acomms::RouteManager::find_next_route_hop(int us, int dest)
{
    int current_route_index = route_index(us);

    if (current_route_index == -1)
    {
        glog.is(DEBUG1) && glog << group("goby::acomms::route")
                                << "Current modem id is not in route." << std::endl;
        return -1;
    }

    int dest_route_index = route_index(dest);

    if (dest_route_index == -1)
    {
        glog.is(DEBUG1) && glog << group("goby::acomms::route")
                                << "Destination modem id is not in route." << std::endl;
        return -1;
    }

    int direction;
    if (dest_route_index < current_route_index)
        direction = -1;
    else if (dest_route_index > current_route_index)
        direction = 1;
    else
        direction = 0;

    int next_hop_index = current_route_index + direction;

    if (next_hop_index < 0 || next_hop_index >= cfg_.route().hop_size())
    {
        glog.is(DEBUG1) && glog << warn << group("goby::acomms::route") << "Next hop index ("
                                << next_hop_index << ") is not in route." << std::endl;
        return -1;
    }

    return cfg_.route().hop(next_hop_index);
}

void goby::acomms::RouteManager::add_subnet_queue(QueueManager* manager)
{
    if (!manager)
    {
        glog.is(DEBUG1) && glog << warn << group("goby::acomms::route")
                                << "Null manager passed to add_subnet_queue. Ignoring."
                                << std::endl;
        return;
    }

    std::uint32_t modem_id = manager->modem_id();
    std::uint32_t subnet = modem_id & cfg_.subnet_mask();

    if (subnet_map_.count(subnet))
        glog.is(DEBUG1) && glog << warn << group("goby::acomms::route") << "Subnet " << subnet
                                << " already mapped. Replacing." << std::endl;

    subnet_map_[subnet] = manager;

    glog.is(DEBUG1) && glog << group("goby::acomms::route") << "Adding subnet (hex: " << std::hex
                            << subnet << std::dec << ")" << std::endl;
}
