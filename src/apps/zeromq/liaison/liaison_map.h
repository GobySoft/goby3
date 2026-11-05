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
#include <filesystem> // for path
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

#include "liaison_map_enc.h" // for enc::Cell

namespace Wt
{
class WComboBox;
class WFileResource;
class WFileUpload;
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
    double pitch{NAN};   // degrees
    double roll{NAN};    // degrees
    double altitude{NAN}; // m
    double x{NAN};        // m, local
    double y{NAN};        // m, local
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

    struct TrackPoint
    {
        LatLon position;
        std::chrono::steady_clock::time_point received;
    };

    struct Vehicle
    {
        VehicleFix fix;
        std::chrono::steady_clock::time_point received;
        bool fix_changed{true};
        bool stale{false};

        std::deque<TrackPoint> unsent_track;
        LatLon last_track_point;
        bool has_track_point{false};

        Wt::WLeafletMap::WidgetMarker* marker{nullptr};
        Wt::WContainerWidget* marker_widget{nullptr};
        Wt::WText* arrow{nullptr};
        Wt::WText* label{nullptr};
        Wt::WText* depth_gauge{nullptr};

        Wt::WTableRow* row{nullptr};
        enum Column
        {
            COLUMN_NAME,
            COLUMN_HEADING,
            COLUMN_SPEED,
            COLUMN_DEPTH,
            COLUMN_PITCH,
            COLUMN_ROLL,
            COLUMN_ALTITUDE,
            COLUMN_LAT,
            COLUMN_LON,
            COLUMN_X,
            COLUMN_Y,
            COLUMN_AGE,
            COLUMN_COUNT
        };
        std::array<Wt::WText*, COLUMN_COUNT> cells{};
    };

    void focus() override;
    void unfocus() override;

    void create_map();
    void create_controls(Wt::WContainerWidget* parent);
    void create_vehicle_list(Wt::WContainerWidget* parent);
    std::string layer_control_js() const;
    std::string tile_layers_js() const;
    std::string enc_js() const;
    void add_enc_cell(const enc::Cell& cell);
    void handle_enc_upload();

    void update_map();
    void add_vehicle_ui(const std::string& name, Vehicle& vehicle);
    void show_column(int column);
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
    void set_trail(int step);
    int trail_seconds() const;

  private:
    const protobuf::ProtobufMapConfig map_cfg_;
    const double update_freq_;
    bool leaflet_configured_{false};
    bool focused_{false};
    bool paused_{false};
    bool fitted_{false};
    bool show_age_;
    bool show_depth_;
    int trail_step_;
    std::chrono::steady_clock::time_point last_trail_age_;
    std::string followed_;

    Wt::WLeafletMap* map_{nullptr};
    std::string js_setup_;
    Wt::WTable* vehicle_table_{nullptr};
    // columns only some sources fill are hidden until a vehicle has a value for them
    std::array<bool, Vehicle::COLUMN_COUNT> column_shown_{};
    Wt::WText* waiting_text_{nullptr};
    Wt::WComboBox* follow_box_{nullptr};
    Wt::WPushButton* pause_button_{nullptr};
    Wt::WText* trail_text_{nullptr};
    const std::filesystem::path enc_dir_;
    Wt::WFileUpload* enc_upload_{nullptr};
    Wt::WText* enc_status_{nullptr};
    std::vector<std::shared_ptr<Wt::WFileResource>> enc_resources_;

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
