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

#include <array>      // for array
#include <chrono>     // for steady_clock
#include <cmath>      // for NAN
#include <deque>      // for deque
#include <functional> // for function
#include <map>        // for map
#include <memory>     // for shared_ptr
#include <string>     // for string
#include <utility>    // for pair
#include <vector>     // for vector

#include <Wt/WLeafletMap.h> // for WLeafletMap
#include <Wt/WSignal.h>     // for JSignal
#include <Wt/WTimer.h>      // for WTimer

#include "goby/middleware/protobuf/frontseat_data.pb.h" // for VehicleType
#include "goby/zeromq/liaison/liaison_container.h"      // for LiaisonContainerWithComms
#include "goby/zeromq/protobuf/liaison_config.pb.h"     // for LiaisonConfig

namespace Wt
{
class WComboBox;
class WContainerWidget;
class WPushButton;
class WTable;
class WTableRow;
class WText;
} // namespace Wt

namespace goby
{
namespace middleware
{
class SerializationSubscriptionRegex;
} // namespace middleware

namespace apps
{
namespace zeromq
{
struct VehicleFix
{
    std::string name;
    double lat{0};
    double lon{0};
    double heading{NAN}; // degrees
    double speed{NAN};   // m/s
    double depth{NAN};   // m
    goby::middleware::frontseat::protobuf::VehicleType type{
        goby::middleware::frontseat::protobuf::UNKNOWN};
};

class MapCommsThread;

class LiaisonMap : public goby::zeromq::LiaisonContainerWithComms<LiaisonMap, MapCommsThread>
{
  public:
    LiaisonMap(const protobuf::LiaisonConfig& cfg);

    void handle_fix(const VehicleFix& fix);

  private:
    using LatLon = std::pair<double, double>;

    struct Vehicle
    {
        VehicleFix fix;
        std::chrono::steady_clock::time_point received;
        bool fix_changed{true};
        bool stale{false};

        std::deque<LatLon> unsent_track;
        LatLon last_track_point;
        bool has_track_point{false};

        Wt::WLeafletMap::WidgetMarker* marker{nullptr};
        Wt::WContainerWidget* marker_widget{nullptr};
        Wt::WText* arrow{nullptr};
        Wt::WText* label{nullptr};

        Wt::WTableRow* row{nullptr};
        enum Column
        {
            COLUMN_NAME = 0,
            COLUMN_HEADING = 1,
            COLUMN_SPEED = 2,
            COLUMN_DEPTH = 3,
            COLUMN_AGE = 4,
            COLUMN_COUNT = 5
        };
        std::array<Wt::WText*, COLUMN_COUNT> cells{};
    };

    void focus() override;
    void unfocus() override;

    void create_map();
    void create_controls(Wt::WContainerWidget* parent);
    void create_vehicle_list(Wt::WContainerWidget* parent);
    void add_tile_layers();
    void add_tile_layer(const protobuf::ProtobufMapConfig::TileLayer& layer);

    void update_map();
    void add_vehicle_ui(const std::string& name, Vehicle& vehicle);
    void update_vehicle_ui(const std::string& name, Vehicle& vehicle, bool stale,
                           std::chrono::steady_clock::duration age);
    std::string row_style(const std::string& name, const Vehicle& vehicle) const;
    std::string label_text(const std::string& name, const Vehicle& vehicle,
                           std::chrono::steady_clock::duration age) const;
    std::string track_js(const std::string& name, Vehicle& vehicle);
    void run_map_js(const std::string& js);
    void update_timer();

    void centre_on(const std::string& name);
    void fit_all();
    void clear_tracks();
    void set_following(const std::string& name);
    void toggle_pause();
    void set_trail_points(int points);

  private:
    const protobuf::ProtobufMapConfig map_cfg_;
    const double update_freq_;
    bool leaflet_configured_{false};
    bool focused_{false};
    bool paused_{false};
    bool fitted_{false};
    bool show_age_;
    int trail_points_;
    std::string followed_;

    Wt::WLeafletMap* map_{nullptr};
    std::string js_setup_;
    Wt::WTable* vehicle_table_{nullptr};
    Wt::WText* waiting_text_{nullptr};
    Wt::WComboBox* follow_box_{nullptr};
    Wt::WPushButton* pause_button_{nullptr};
    Wt::WText* trail_text_{nullptr};

    std::map<std::string, Vehicle> vehicles_;

    Wt::WTimer map_timer_;
    Wt::JSignal<> user_drag_;
};

class MapCommsThread : public goby::zeromq::LiaisonCommsThread<LiaisonMap>
{
  public:
    MapCommsThread(LiaisonMap* map, const protobuf::LiaisonConfig& config, int index);

  private:
    void subscribe(const protobuf::ProtobufMapConfig::Source& source);

  private:
    LiaisonMap* map_;
    std::vector<std::shared_ptr<goby::middleware::SerializationSubscriptionRegex>> subscriptions_;
};

} // namespace zeromq
} // namespace apps
} // namespace goby

#endif
