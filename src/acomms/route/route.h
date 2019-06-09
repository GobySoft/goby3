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

#ifndef ROUTE20120410
#define ROUTE20120410

#include "goby/acomms/protobuf/queue.pb.h"
#include "goby/acomms/protobuf/route.pb.h"
#include "goby/acomms/queue/queue_manager.h"

namespace goby
{
namespace acomms
{
class RouteManager
{
  public:
    RouteManager() { glog.add_group("goby::acomms::route", util::Colors::yellow); }
    ~RouteManager() {}

    void set_cfg(const protobuf::RouteManagerConfig& cfg);
    void merge_cfg(const protobuf::RouteManagerConfig& cfg);

    void handle_in(const protobuf::QueuedMessageMeta& meta,
                   const google::protobuf::Message& data_msg, int modem_id);

    void handle_out(protobuf::QueuedMessageMeta* meta, const google::protobuf::Message& data_msg,
                    int modem_id);

    void add_subnet_queue(QueueManager* manager);

    bool is_in_route(int modem_id) { return route_index(modem_id) != -1; }

    int route_index(int modem_id);

    int find_next_hop(int us, int dest);
    int find_next_route_hop(int us, int dest);

  private:
    RouteManager(const RouteManager&);
    RouteManager& operator=(const RouteManager&);

    void process_cfg();

  private:
    protobuf::RouteManagerConfig cfg_;

    // maps (modem_id & subnet_mask) onto QueueManager
    std::map<std::uint32_t, QueueManager*> subnet_map_;
};
} // namespace acomms
} // namespace goby

#endif
