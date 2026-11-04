// Copyright 2026:
//   GobySoft, LLC (2013-)
//   Community contributors (see AUTHORS file)
// File authors:
//   Toby Schneider <toby@gobysoft.org>
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

#ifndef GOBY_APPS_ZEROMQ_LIAISON_LIAISON_MAP_H
#define GOBY_APPS_ZEROMQ_LIAISON_LIAISON_MAP_H

#include "goby/zeromq/liaison/liaison_container.h"  // for LiaisonContainerWithComms
#include "goby/zeromq/protobuf/liaison_config.pb.h" // for LiaisonConfig

namespace Wt
{
class WLeafletMap;
} // namespace Wt

namespace goby
{
namespace apps
{
namespace zeromq
{
class MapCommsThread;

class LiaisonMap : public goby::zeromq::LiaisonContainerWithComms<LiaisonMap, MapCommsThread>
{
  public:
    LiaisonMap(const protobuf::LiaisonConfig& cfg);

  private:
    void focus() override;

    void create_map();
    void add_tile_layers();
    void add_tile_layer(const protobuf::ProtobufMapConfig::TileLayer& layer);

  private:
    const protobuf::ProtobufMapConfig map_cfg_;
    bool leaflet_configured_{false};
    Wt::WLeafletMap* map_{nullptr};
};

class MapCommsThread : public goby::zeromq::LiaisonCommsThread<LiaisonMap>
{
  public:
    MapCommsThread(LiaisonMap* map, const protobuf::LiaisonConfig& config, int index)
        : LiaisonCommsThread<LiaisonMap>(map, config, index)
    {
    }
};

} // namespace zeromq
} // namespace apps
} // namespace goby

#endif
