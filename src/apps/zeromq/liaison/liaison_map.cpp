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

#include <string> // for string
#include <vector> // for vector

#include <Wt/Json/Object.h>  // for Object
#include <Wt/Json/Value.h>   // for Value
#include <Wt/WApplication.h> // for WApplication
#include <Wt/WLeafletMap.h>  // for WLeafletMap
#include <Wt/WLength.h>      // for WLength
#include <Wt/WString.h>      // for WString
#include <Wt/WText.h>        // for WText

#include "goby/util/debug_logger/flex_ostream.h" // for glog

#include "liaison_map.h"

using goby::glog;
using TileLayer = goby::apps::zeromq::protobuf::ProtobufMapConfig::TileLayer;

namespace
{
bool has_configuration_property(const std::string& name)
{
    std::string value;
    return Wt::WApplication::readConfigurationProperty(name, value) && !value.empty();
}

google::protobuf::RepeatedPtrField<TileLayer> default_tile_layers()
{
    google::protobuf::RepeatedPtrField<TileLayer> layers;

    layers.Add(); // OpenStreetMap from the field defaults

    auto* seamarks = layers.Add();
    seamarks->set_url_template("https://tiles.openseamap.org/seamark/{z}/{x}/{y}.png");
    seamarks->set_attribution(
        "&copy; <a href=\"https://www.openseamap.org\">OpenSeaMap</a> contributors");
    seamarks->set_max_zoom(18);
    seamarks->set_overlay(true);

    return layers;
}
} // namespace

goby::apps::zeromq::LiaisonMap::LiaisonMap(const protobuf::LiaisonConfig& cfg)
    : LiaisonContainerWithComms<LiaisonMap, MapCommsThread>(cfg), map_cfg_(cfg.pb_map_config())
{
    set_name("Map");

    // WLeafletMap throws without these, which would take down the whole Liaison session
    leaflet_configured_ =
        has_configuration_property("leafletJSURL") && has_configuration_property("leafletCSSURL");

    if (!leaflet_configured_)
    {
        glog.is_warn() && glog << "Liaison Map: leafletJSURL and leafletCSSURL are not set in "
                                  "wt_config.xml, so the map is disabled"
                               << std::endl;
        addNew<Wt::WText>("The map needs the Leaflet library. Set the <tt>leafletJSURL</tt> and "
                          "<tt>leafletCSSURL</tt> properties in <tt>wt_config.xml</tt>.");
    }
}

void goby::apps::zeromq::LiaisonMap::focus()
{
    // created on first view: Wt treats a Leaflet script that fails to load (e.g. an unreachable
    // CDN) as fatal to the session, which must not break the other tabs
    if (leaflet_configured_ && !map_)
        create_map();

    // Leaflet measures its container only when created or on window resize,
    // either of which can happen while this tab is hidden
    if (map_)
        Wt::WApplication::instance()->doJavaScript("{var m=" + map_->mapJsRef() +
                                                   ";if(m)m.invalidateSize();}");
}

void goby::apps::zeromq::LiaisonMap::create_map()
{
    map_ = addNew<Wt::WLeafletMap>();
    map_->setStyleClass("liaison-map");
    map_->resize(Wt::WLength::Auto, Wt::WLength(map_cfg_.map_height()));

    add_tile_layers();

    map_->panTo(Wt::WLeafletMap::Coordinate(map_cfg_.initial_lat(), map_cfg_.initial_lon()));
    map_->setZoomLevel(map_cfg_.initial_zoom());
}

void goby::apps::zeromq::LiaisonMap::add_tile_layers()
{
    if (map_cfg_.blank_background())
        return;

    const auto layers =
        map_cfg_.tile_layer_size() > 0 ? map_cfg_.tile_layer() : default_tile_layers();

    const TileLayer* base = nullptr;
    std::vector<const TileLayer*> overlays;
    for (const auto& layer : layers)
    {
        if (layer.overlay())
        {
            overlays.push_back(&layer);
        }
        else if (!base)
        {
            base = &layer;
        }
        else
        {
            glog.is_warn() && glog << "Liaison Map: only the first base tile_layer is shown, "
                                      "ignoring: "
                                   << layer.url_template() << std::endl;
        }
    }

    // Leaflet draws tile layers in the order they are added
    if (base)
        add_tile_layer(*base);
    for (const auto* overlay : overlays) add_tile_layer(*overlay);
}

void goby::apps::zeromq::LiaisonMap::add_tile_layer(const TileLayer& layer)
{
    Wt::Json::Object options;
    options["attribution"] = Wt::Json::Value(Wt::WString::fromUTF8(layer.attribution()));
    options["minZoom"] = Wt::Json::Value(layer.min_zoom());
    options["maxZoom"] = Wt::Json::Value(layer.max_zoom());
    options["opacity"] = Wt::Json::Value(layer.opacity());
    map_->addTileLayer(layer.url_template(), options);
}
