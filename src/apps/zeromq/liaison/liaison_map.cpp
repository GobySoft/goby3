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

#include <algorithm> // for min, max
#include <cmath>     // for isnan, lround, cos, sqrt
#include <cstdio>    // for snprintf
#include <cstdlib>   // for abs
#include <iomanip>   // for setprecision
#include <iterator>  // for distance
#include <regex>     // for regex_replace
#include <sstream>   // for ostringstream
#include <string>    // for string
#include <vector>    // for vector

#include <Wt/Json/Array.h>       // for Array
#include <Wt/Json/Object.h>      // for Object
#include <Wt/Json/Serializer.h>  // for serialize
#include <Wt/Json/Value.h>       // for Value
#include <Wt/WApplication.h>     // for WApplication
#include <Wt/WCheckBox.h>        // for WCheckBox
#include <Wt/WComboBox.h>        // for WComboBox
#include <Wt/WContainerWidget.h> // for WContainerWidget
#include <Wt/WFileResource.h>    // for WFileResource
#include <Wt/WFileUpload.h>      // for WFileUpload
#include <Wt/WLength.h>          // for WLength
#include <Wt/WPushButton.h>      // for WPushButton
#include <Wt/WSlider.h>          // for WSlider
#include <Wt/WString.h>          // for WString
#include <Wt/WTable.h>           // for WTable
#include <Wt/WTableCell.h>       // for WTableCell
#include <Wt/WTableRow.h>        // for WTableRow
#include <Wt/WText.h>            // for WText
#include <Wt/WWebWidget.h>       // for jsStringLiteral

#include "goby/middleware/frontseat/groups.h"      // for node_status
#include "goby/middleware/gpsd/groups.h"           // for tpv
#include "goby/middleware/marshalling/interface.h" // for MarshallingScheme
#include "goby/middleware/protobuf/gpsd.pb.h"      // for TimePositionVelocity
#include "goby/util/debug_logger/flex_ostream.h"   // for glog

#include "liaison_map.h"

using goby::glog;
using goby::middleware::frontseat::protobuf::VehicleType;
using Source = goby::apps::zeromq::protobuf::ProtobufMapConfig::Source;
using TileLayer = goby::apps::zeromq::protobuf::ProtobufMapConfig::TileLayer;

namespace
{
bool has_configuration_property(const std::string& name)
{
    std::string value;
    return Wt::WApplication::readConfigurationProperty(name, value) && !value.empty();
}

// GEBCO covers the world, coarsely; the NOAA chart overlay is transparent outside its coverage,
// so together they give a chart in US waters and bathymetry everywhere else
google::protobuf::RepeatedPtrField<TileLayer> default_tile_layers()
{
    google::protobuf::RepeatedPtrField<TileLayer> layers;

    auto* gebco = layers.Add();
    gebco->set_name("GEBCO bathymetry");
    gebco->set_url_template("https://wms.gebco.net/mapserv");
    gebco->mutable_wms()->set_layers("GEBCO_LATEST");
    gebco->set_attribution(
        "Imagery reproduced from the <a href=\"https://www.gebco.net\">GEBCO</a> "
        "Grid, GEBCO Compilation Group");

    layers.Add()->set_name("OpenStreetMap"); // the field defaults

    auto* esri = layers.Add();
    esri->set_name("Esri Ocean");
    esri->set_url_template("https://server.arcgisonline.com/ArcGIS/rest/services/Ocean/"
                           "World_Ocean_Base/MapServer/tile/{z}/{y}/{x}");
    esri->set_attribution("Tiles &copy; Esri &mdash; Sources: GEBCO, NOAA, CHS, OSU, UNH, CSUMB, "
                          "National Geographic, DeLorme, NAVTEQ, and Esri");
    esri->set_max_native_zoom(16);

    // NOAA's soundings share a layer with its depth areas and contours, so depths and the rest
    // of the chart are separate overlays
    const std::string noaa_url = "https://gis.charttools.noaa.gov/arcgis/rest/services/MCS/"
                                 "NOAAChartDisplay/MapServer/exts/MaritimeChartService/WMSServer";
    const std::string noaa_attribution =
        "Chart: <a href=\"https://nauticalcharts.noaa.gov\">NOAA Office of Coast Survey</a>, "
        "not for navigation";

    auto* noaa_depths = layers.Add();
    noaa_depths->set_name("NOAA depths");
    noaa_depths->set_url_template(noaa_url);
    noaa_depths->mutable_wms()->set_layers("2");
    noaa_depths->set_tile_size(512);
    noaa_depths->set_depth_units("feet");
    noaa_depths->set_overlay(true);
    noaa_depths->set_attribution(noaa_attribution);

    auto* noaa = layers.Add();
    noaa->set_name("NOAA chart");
    noaa->set_url_template(noaa_url);
    // without data quality, low accuracy, additional information, shallow water pattern and
    // overscale, which hatch most of a coastal display
    noaa->mutable_wms()->set_layers("0,1,3,4,5,6,7");
    noaa->set_tile_size(512);
    noaa->set_overlay(true);
    noaa->set_attribution(noaa_attribution);

    auto* seamarks = layers.Add();
    seamarks->set_name("OpenSeaMap seamarks");
    seamarks->set_url_template("https://tiles.openseamap.org/seamark/{z}/{x}/{y}.png");
    seamarks->set_attribution(
        "&copy; <a href=\"https://www.openseamap.org\">OpenSeaMap</a> contributors");
    seamarks->set_max_zoom(18);
    seamarks->set_overlay(true);
    // NOAA's chart already shows the buoys and lights in its coverage
    seamarks->set_shown(false);

    return layers;
}

std::string layer_name(const TileLayer& layer)
{
    if (layer.has_name())
        return layer.name();
    static const std::regex host("^[a-z]+://([^/]+)");
    std::smatch match;
    if (std::regex_search(layer.url_template(), match, host))
        return match[1];
    return layer.url_template();
}

std::string exact_regex(const std::string& s)
{
    static const std::regex special_chars{R"([-[\]{}()*+?.,\^$|#\s])"};
    return "^" + std::regex_replace(s, special_chars, R"(\$&)") + "$";
}

// a depth gauge says nothing about a vessel that stays at the surface
bool has_depth_gauge(const goby::apps::zeromq::VehicleFix& fix)
{
    using namespace goby::middleware::frontseat::protobuf;
    switch (fix.type)
    {
        case USV:
        case USV_POWERED:
        case USV_SAILING:
        case SHIP:
        case BUOY:
        case MOORING:
        case MOORING_SURFACE: return false;
        default: return !std::isnan(fix.depth);
    }
}

std::string vehicle_colour(VehicleType type)
{
    using namespace goby::middleware::frontseat::protobuf;
    switch (type)
    {
        case AUV:
        case GLIDER:
        case ROV: return "#1c9fcb";
        case USV:
        case USV_POWERED:
        case USV_SAILING: return "#e36034";
        case SHIP: return "#4a5560";
        case BUOY:
        case MOORING:
        case MOORING_SUBSURFACE:
        case MOORING_SURFACE: return "#2f8f4e";
        case TARGET: return "#b7322c";
        default: return "#7a7f85";
    }
}

// a log scale shows a few metres below the surface as clearly as a few thousand
std::string depth_gauge_svg(double depth)
{
    depth = std::max(0.0, depth);
    constexpr double bottom = 5000, height = 30, width = 6;
    auto y = [](double d)
    { return height * std::log10(1 + std::max(0.0, d)) / std::log10(1 + bottom); };

    std::ostringstream svg;
    svg << std::fixed << std::setprecision(1)
        << "<svg width=\"56\" height=\"32\" viewBox=\"-1 -1 56 32\">"
        << "<title>depth " << depth << " m</title>"
        << "<rect width=\"" << width << "\" height=\"" << height << "\" fill=\"#eef3f7\"/>"
        << "<rect width=\"" << width << "\" height=\"" << std::min(height, y(depth))
        << "\" fill=\"#1d4f86\"/>"
        << "<rect width=\"" << width << "\" height=\"" << height
        << "\" fill=\"none\" stroke=\"#4b5a68\" stroke-width=\"0.8\"/>";
    for (double tick : {10.0, 100.0, 1000.0})
        svg << "<line x1=\"" << width << "\" x2=\"" << width + 3 << "\" y1=\"" << y(tick)
            << "\" y2=\"" << y(tick) << "\" stroke=\"#4b5a68\" stroke-width=\"0.8\"/>";
    // the value sits beside the bar at the depth it marks, kept inside the gauge's height
    svg << "<text x=\"" << width + 4 << "\" y=\"" << std::clamp(y(depth) + 3, 8.0, height)
        << "\" font-size=\"9\" class=\"map-depth-value\">" << std::setprecision(depth < 10 ? 1 : 0)
        << depth << " m</text></svg>";
    return svg.str();
}

std::string arrow_svg(double heading, const std::string& colour)
{
    std::ostringstream svg;
    svg << "<svg width=\"24\" height=\"24\" viewBox=\"-12 -12 24 24\">";
    if (std::isnan(heading))
        svg << "<circle r=\"6\"";
    else
        svg << "<path transform=\"rotate(" << ((std::lround(heading) % 360) + 360) % 360
            << ")\" d=\"M0,-10L7,8L0,4L-7,8Z\" stroke-linejoin=\"round\"";
    svg << " fill=\"" << colour << "\" stroke=\"#fff\" stroke-width=\"1.5\"/></svg>";
    return svg.str();
}

std::string format_number(const char* format, double value)
{
    if (std::isnan(value))
        return "-";
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), format, value);
    std::string formatted(buffer);
    // a surface vehicle's depth is often a tiny negative number, which would print as "-0.0"
    if (formatted.front() == '-' && formatted.find_first_of("123456789") == std::string::npos)
        formatted.erase(0, 1);
    return formatted;
}

std::string format_heading(double heading)
{
    if (std::isnan(heading))
        return "-";
    char buffer[8];
    std::snprintf(buffer, sizeof(buffer), "%03ld", ((std::lround(heading) % 360) + 360) % 360);
    return buffer;
}

std::string format_age(std::chrono::steady_clock::duration age)
{
    const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(age).count();
    if (seconds < 120)
        return std::to_string(seconds) + " s";
    return std::to_string(seconds / 60) + " min";
}

// the Trail slider's steps in seconds; the last (-1) draws every kept point
constexpr std::array<int, 11> trail_steps{0, 30, 60, 120, 300, 600, 900, 1800, 3600, 7200, -1};

int nearest_trail_step(int seconds)
{
    if (seconds < 0)
        return trail_steps.size() - 1;
    int nearest = 0;
    for (int i = 1, n = trail_steps.size() - 1; i < n; ++i)
    {
        if (std::abs(trail_steps[i] - seconds) < std::abs(trail_steps[nearest] - seconds))
            nearest = i;
    }
    return nearest;
}

std::string trail_label(int seconds)
{
    if (seconds < 0)
        return "All";
    if (seconds == 0)
        return "Off";
    if (seconds < 60)
        return std::to_string(seconds) + " s";
    if (seconds < 3600)
        return std::to_string(seconds / 60) + " min";
    return std::to_string(seconds / 3600) + " h";
}

// equirectangular approximation, ample for the metre-scale track threshold
double distance_meters(const std::pair<double, double>& a, const std::pair<double, double>& b)
{
    const double earth_radius = 6371e3;
    const double deg = M_PI / 180;
    const double x = (b.second - a.second) * deg * std::cos((a.first + b.first) / 2 * deg);
    const double y = (b.first - a.first) * deg;
    return earth_radius * std::sqrt(x * x + y * y);
}
} // namespace

goby::apps::zeromq::LiaisonMap::LiaisonMap(const protobuf::LiaisonConfig& cfg)
    : LiaisonContainerWithComms<LiaisonMap, MapCommsThread>(cfg),
      map_cfg_(cfg.pb_map_config()),
      update_freq_(cfg.update_freq()),
      show_age_(map_cfg_.show_age()),
      show_depth_(map_cfg_.show_depth()),
      trail_step_(nearest_trail_step(map_cfg_.trail_seconds())),
      enc_dir_(enc::directory(map_cfg_)),
      user_drag_(this, "userDrag")
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

    map_timer_.setInterval(std::chrono::milliseconds(static_cast<long>(1.0e3 / update_freq_)));
    map_timer_.timeout().connect([this]() { update_map(); });

    // panning programmatically also moves the map, so only a drag means the user took over
    user_drag_.connect([this]() { set_following(""); });
}

void goby::apps::zeromq::LiaisonMap::handle_fix(const VehicleFix& fix)
{
    auto& vehicle = vehicles_[fix.name];
    vehicle.fix = fix;
    vehicle.received = std::chrono::steady_clock::now();
    vehicle.fix_changed = true;

    const LatLon point(fix.lat, fix.lon);
    if (!vehicle.has_track_point ||
        distance_meters(vehicle.last_track_point, point) >= map_cfg_.track_min_move_meters())
    {
        vehicle.unsent_track.push_back({point, vehicle.received});
        vehicle.last_track_point = point;
        vehicle.has_track_point = true;
        while (vehicle.unsent_track.size() > static_cast<std::size_t>(map_cfg_.track_length()))
            vehicle.unsent_track.pop_front();
    }
}

void goby::apps::zeromq::LiaisonMap::focus()
{
    focused_ = true;

    // created on first view: Wt treats a Leaflet script that fails to load (e.g. an unreachable
    // CDN) as fatal to the session, which must not break the other tabs
    if (leaflet_configured_ && !map_)
        create_map();

    if (map_)
    {
        // Leaflet measures its container only when created or on window resize,
        // either of which can happen while this tab is hidden
        run_map_js("m.invalidateSize();");
        update_map();
    }
    update_timer();
}

void goby::apps::zeromq::LiaisonMap::unfocus()
{
    focused_ = false;
    update_timer();
}

void goby::apps::zeromq::LiaisonMap::create_map()
{
    auto* panel = addNew<Wt::WContainerWidget>();
    panel->setStyleClass("map-panel");

    create_controls(panel);

    map_ = panel->addNew<Wt::WLeafletMap>();
    map_->setStyleClass("liaison-map");
    map_->resize(Wt::WLength::Auto, Wt::WLength(map_cfg_.map_height()));

    map_->panTo(Wt::WLeafletMap::Coordinate(map_cfg_.initial_lat(), map_cfg_.initial_lon()));
    map_->setZoomLevel(map_cfg_.initial_zoom());

    create_vehicle_list(panel);

    // tracks are drawn with Leaflet directly: WLeafletMap polylines cannot be extended or removed
    // each point carries the browser time it was received, from the age Liaison sends with it,
    // so the server's and browser's clocks need not agree. S is the trail in seconds (<0: all).
    js_setup_ = layer_control_js() + tile_layers_js() +
                "if(!m.gobyTracks){m.gobyTracks={};"
                "m.gobyShow=function(t,S,force){var i=0;"
                "if(S>=0){var c=Date.now()-1e3*S;while(i<t.all.length&&t.all[i].t<c)++i;}"
                "if(force||i!==t.first){t.first=i;"
                "t.setLatLngs(t.all.slice(i).map(function(q){return q.ll;}));}};"
                "m.gobyTrack=function(n,c,p,N,S){var t=m.gobyTracks[n];"
                "if(!t){t=m.gobyTracks[n]=L.polyline([],{color:c,weight:2,opacity:0.8,"
                "interactive:false,className:'map-track'}).addTo(m);t.all=[];}"
                "var now=Date.now();t.all=t.all.concat(p.map(function(q){"
                "return {ll:L.latLng(q[0],q[1]),t:now-q[2]};})).slice(-N);m.gobyShow(t,S,true);};"
                "m.gobyTrail=function(S,force){"
                "for(var n in m.gobyTracks)m.gobyShow(m.gobyTracks[n],S,force);};"
                "m.gobyClearTracks=function(){for(var n in m.gobyTracks){"
                "m.gobyTracks[n].all=[];m.gobyTracks[n].setLatLngs([]);}};"
                "m.on('dragstart',function(){" +
                user_drag_.createCall({}) + "});}" + enc_js();

    for (const auto& cell : enc::stored_cells(enc_dir_)) add_enc_cell(cell);
}

std::string goby::apps::zeromq::LiaisonMap::enc_js() const
{
    // A simplified chart: depth areas shaded by their shallow limit, land, contours, soundings
    // (from zoom 15, as there are thousands) and aids to navigation coloured by their COLOUR.
    // Panes keep the chart under the tracks and vehicles whatever order the layers load in.
    return "if(!m.gobyAddEnc){m.gobyEnc={};"
           "[['gobyEncArea',350],['gobyEncLine',360],['gobyEncText',370],['gobyEncAids',380]]"
           ".forEach(function(p){m.createPane(p[0]).style.zIndex=p[1];});"
           "var fill=function(d){return d<0?'#a9c79b':d<2?'#7fb2de':d<5?'#a4c9eb':"
           "d<10?'#c5ddf2':d<20?'#e1edf8':'#fbfdff';},"
           "colour={'1':'#ffffff','2':'#222222','3':'#d7263d','4':'#1f9d55','6':'#f2d21b',"
           "'11':'#f28c28'},hazard={OBSTRN:'Obstruction',WRECKS:'Wreck',UWTROC:'Rock'};"
           "var make=function(k,d){"
           "if(k==='SOUNDG')return L.geoJSON(d,{pointToLayer:function(f,ll){"
           "var z=f.properties.DEPTH;return L.marker(ll,{pane:'gobyEncText',interactive:false,"
           "icon:L.divIcon({className:'map-enc-sounding',iconSize:null,"
           "html:'<span>'+(z<31?z.toFixed(1):Math.round(z))+'</span>'})});}});"
           "if(k==='DEPARE'||k==='DRGARE')return L.geoJSON(d,{pane:'gobyEncArea',"
           "interactive:false,style:function(f){return {stroke:k==='DRGARE',color:'#8a9aa8',"
           "weight:1,dashArray:'4 3',fillColor:fill(f.properties.DRVAL1),fillOpacity:1};}});"
           "if(k==='LNDARE')return L.geoJSON(d,{pane:'gobyEncArea',interactive:false,"
           "style:{color:'#8b7d55',weight:1,fillColor:'#f1e3b3',fillOpacity:1}});"
           "if(k==='DEPCNT')return L.geoJSON(d,{pane:'gobyEncLine',style:{color:'#6f8fae',"
           "weight:1},onEachFeature:function(f,l){if(f.properties.VALDCO!=null)"
           "l.bindTooltip(f.properties.VALDCO+' m',{sticky:true});}});"
           "if(k==='COALNE')return L.geoJSON(d,{pane:'gobyEncLine',interactive:false,"
           "style:{color:'#4a4030',weight:1.5}});"
           "return L.geoJSON(d,{pane:'gobyEncAids',style:{color:'#333',weight:1,"
           "dashArray:'2 2',fill:false},pointToLayer:function(f,ll){var p=f.properties,"
           "c=p.COLOUR&&p.COLOUR.length?colour[p.COLOUR[0]]||'#999':'#999';"
           "return L.circleMarker(ll,{pane:'gobyEncAids',radius:k==='LIGHTS'||hazard[k]?3:5,"
           "color:'#222',weight:1,fillOpacity:1,"
           "fillColor:k==='LIGHTS'?'#d000d0':hazard[k]?'#fff':c});},"
           "onEachFeature:function(f,l){var p=f.properties,"
           "t=p.OBJNAM||hazard[k]||(k==='LIGHTS'?'Light':k);"
           "if(p.VALSOU!=null)t+=' '+p.VALSOU+' m';l.bindTooltip(t);}});};"
           "m.gobyAddEnc=function(n,urls,units){var g=L.layerGroup(),old=(m.gobyEncSound||{})[n];"
           "m.gobyAddOverlay(g,n,true);"
           "if(!m.gobyEncSoundings){m.gobyEncSoundings=L.layerGroup();m.gobyEncSound={};"
           "m.on('zoomend',function(){for(var c in m.gobyEncSound){var s=m.gobyEncSound[c];"
           "if(!s)continue;if(m.getZoom()>=15)m.gobyEncSoundings.addLayer(s);"
           "else m.gobyEncSoundings.removeLayer(s);}});"
           "m.gobyAddOverlay(m.gobyEncSoundings,'ENC soundings',true);}"
           "if(old){m.gobyEncSoundings.removeLayer(old);m.gobyUnitLayers=m.gobyUnitLayers.filter("
           "function(e){return e[0]!==old;});}m.gobyEncSound[n]=null;"
           "Object.keys(urls).forEach(function(k){fetch(urls[k]).then(function(r){"
           "return r.json();}).then(function(d){var l=make(k,d);if(k==='SOUNDG'){"
           "m.gobyEncSound[n]=l;if(units)m.gobyUnitLayers.push([l,n+' soundings in '+units]);"
           "if(m.getZoom()>=15)m.gobyEncSoundings.addLayer(l);m.gobyShowUnits();return;}"
           "g.addLayer(l);});});};}";
}

void goby::apps::zeromq::LiaisonMap::add_enc_cell(const enc::Cell& cell)
{
    Wt::Json::Object urls;
    for (const auto& [layer, file] : cell.layers)
    {
        auto resource = std::make_shared<Wt::WFileResource>("application/geo+json", file.string());
        urls[layer] = Wt::Json::Value(Wt::WString::fromUTF8(resource->url()));
        enc_resources_.push_back(resource);
    }

    // the Leaflet map may not exist yet when the tab is first shown
    Wt::WApplication::instance()->doJavaScript(
        "(function f(){var m=" + map_->mapJsRef() + ";if(m){" + js_setup_ + "m.gobyAddEnc(" +
        Wt::WWebWidget::jsStringLiteral("ENC " + cell.name) + "," + Wt::Json::serialize(urls, 0) +
        "," + Wt::WWebWidget::jsStringLiteral(cell.depth_units) +
        ");}else setTimeout(f,100);})();");
}

void goby::apps::zeromq::LiaisonMap::handle_enc_upload()
{
    try
    {
        std::string added;
        for (const auto& cell : enc::import(enc_upload_->spoolFileName(),
                                            enc_upload_->clientFileName().toUTF8(), enc_dir_))
        {
            if (map_)
                add_enc_cell(cell);
            added += (added.empty() ? "" : ", ") + cell.name;
        }
        enc_status_->setText(Wt::WString::fromUTF8("Added " + added));
    }
    catch (const std::exception& e)
    {
        glog.is_warn() && glog << "Liaison Map: ENC upload: " << e.what() << std::endl;
        enc_status_->setText(Wt::WString::fromUTF8(e.what()));
    }
}

void goby::apps::zeromq::LiaisonMap::create_controls(Wt::WContainerWidget* parent)
{
    auto* controls = parent->addNew<Wt::WContainerWidget>();
    controls->setStyleClass("map-controls");

    controls->addNew<Wt::WText>("Follow:");
    follow_box_ = controls->addNew<Wt::WComboBox>();
    follow_box_->addStyleClass("map-follow");
    follow_box_->addItem("None");
    follow_box_->changed().connect(
        [this]()
        {
            set_following(follow_box_->currentIndex() == 0 ? ""
                                                           : follow_box_->currentText().toUTF8());
        });

    auto* fit_button = controls->addNew<Wt::WPushButton>("Fit all");
    fit_button->addStyleClass("map-fit");
    fit_button->clicked().connect([this]() { fit_all(); });

    auto* clear_button = controls->addNew<Wt::WPushButton>("Clear tracks");
    clear_button->addStyleClass("map-clear");
    clear_button->clicked().connect([this]() { clear_tracks(); });

    pause_button_ = controls->addNew<Wt::WPushButton>("Pause");
    pause_button_->addStyleClass("map-pause");
    pause_button_->clicked().connect([this]() { toggle_pause(); });

    auto* age_box = controls->addNew<Wt::WCheckBox>("Show age");
    age_box->addStyleClass("map-show-age");
    age_box->setChecked(show_age_);
    age_box->changed().connect(
        [this, age_box]()
        {
            show_age_ = age_box->isChecked();
            const auto now = std::chrono::steady_clock::now();
            for (auto& [name, vehicle] : vehicles_)
            {
                if (vehicle.label)
                    vehicle.label->setText(
                        Wt::WString::fromUTF8(label_text(name, vehicle, now - vehicle.received)));
            }
        });

    auto* depth_box = controls->addNew<Wt::WCheckBox>("Show depth");
    depth_box->addStyleClass("map-show-depth");
    depth_box->setChecked(show_depth_);
    depth_box->changed().connect(
        [this, depth_box]()
        {
            show_depth_ = depth_box->isChecked();
            for (auto& [name, vehicle] : vehicles_)
            {
                if (vehicle.depth_gauge)
                    vehicle.depth_gauge->setHidden(!show_depth_ || !has_depth_gauge(vehicle.fix));
            }
        });

    controls->addNew<Wt::WText>("Trail:");
    auto* trail_slider = controls->addNew<Wt::WSlider>();
    trail_slider->addStyleClass("map-trail");
    trail_slider->setNativeControl(true);
    trail_slider->setRange(0, trail_steps.size() - 1);
    trail_slider->setValue(trail_step_);
    trail_slider->valueChanged().connect([this](int step) { set_trail(step); });
    trail_slider->sliderMoved().connect([this](int step) { set_trail(step); });
    trail_text_ = controls->addNew<Wt::WText>();
    trail_text_->setStyleClass("map-trail-points");
    set_trail(trail_step_);

    if (map_cfg_.enc_upload())
    {
        // grouped so the label wraps with its button
        auto* enc = controls->addNew<Wt::WContainerWidget>();
        enc->setStyleClass("map-enc");
        enc->addNew<Wt::WText>("Add ENC:");
        enc_upload_ = enc->addNew<Wt::WFileUpload>();
        enc_upload_->addStyleClass("map-enc-upload");
        enc_upload_->setFilters(".000,.zip");
        enc_status_ = enc->addNew<Wt::WText>();
        enc_status_->setStyleClass("map-enc-status");
        enc_upload_->changed().connect(
            [this]()
            {
                enc_status_->setText("Uploading...");
                enc_upload_->upload();
            });
        enc_upload_->uploaded().connect([this]() { handle_enc_upload(); });
        enc_upload_->fileTooLarge().connect(
            [this](::int64_t)
            {
                enc_status_->setText(
                    "Too large for the server (max-request-size in wt_config.xml)");
            });
    }
}

void goby::apps::zeromq::LiaisonMap::create_vehicle_list(Wt::WContainerWidget* parent)
{
    vehicle_table_ = parent->addNew<Wt::WTable>();
    vehicle_table_->setStyleClass("map-vehicles");
    vehicle_table_->setHeaderCount(1);
    const std::array<const char*, Vehicle::COLUMN_COUNT> headers{
        "Vehicle", "Hdg (\u00b0)", "Spd (m/s)", "Depth (m)", "Pitch (\u00b0)", "Roll (\u00b0)",
        "Alt (m)", "Lat",          "Lon",       "X (m)",     "Y (m)",          "Age"};
    column_shown_.fill(true);
    for (int column : {Vehicle::COLUMN_PITCH, Vehicle::COLUMN_ROLL, Vehicle::COLUMN_ALTITUDE,
                       Vehicle::COLUMN_X, Vehicle::COLUMN_Y})
        column_shown_[column] = false;
    for (int column = 0; column < Vehicle::COLUMN_COUNT; ++column)
    {
        vehicle_table_->elementAt(0, column)->addNew<Wt::WText>(
            Wt::WString::fromUTF8(headers[column]));
        vehicle_table_->elementAt(0, column)->setHidden(!column_shown_[column]);
    }

    waiting_text_ = parent->addNew<Wt::WText>("Waiting for vehicle positions.");
    waiting_text_->setStyleClass("map-waiting");
}

std::string goby::apps::zeromq::LiaisonMap::layer_control_js() const
{
    // Every overlay (tile layers, ENC charts and soundings) registers here, so the viewer's
    // choices are kept in the browser for all of them; layers registered later still find their
    // saved state. Charts label depths in their own units (NOAA's in feet, most ENCs in metres),
    // so the corner names them for each shown layer that labels depths.
    return "if(!m.gobyOverlays){m.gobyOverlays={};m.gobyBases={};m.gobyUnitLayers=[];"
           "var key='goby-liaison-map-layers';m.gobySaved={overlays:{}};"
           "try{m.gobySaved=JSON.parse(localStorage.getItem(key))||m.gobySaved;"
           "m.gobySaved.overlays=m.gobySaved.overlays||{};}catch(e){}"
           "m.gobyControl=function(){if(!m.gobyLayers)m.gobyLayers=L.control.layers("
           "Object.keys(m.gobyBases).length>1?m.gobyBases:{},{}).addTo(m);return m.gobyLayers;};"
           "m.gobyAddOverlay=function(l,n,shown){var o=m.gobyOverlays[n];if(o){m.removeLayer(o);"
           "m.gobyControl().removeLayer(o);}m.gobyOverlays[n]=l;m.gobyControl().addOverlay(l,n);"
           "if(n in m.gobySaved.overlays?m.gobySaved.overlays[n]:shown)l.addTo(m);};"
           "m.on('baselayerchange overlayadd overlayremove',function(){var st=m.gobySaved;"
           "for(var n in m.gobyBases)if(m.hasLayer(m.gobyBases[n]))st.base=n;"
           "for(var n in m.gobyOverlays)st.overlays[n]=m.hasLayer(m.gobyOverlays[n]);"
           "try{localStorage.setItem(key,JSON.stringify(st));}catch(e){}});"
           "var U=L.control({position:'bottomleft'});"
           "U.onAdd=function(){return L.DomUtil.create('div','map-depth-units');};U.addTo(m);"
           "m.gobyShowUnits=function(){var t=m.gobyUnitLayers.filter(function(e){"
           "return m.hasLayer(e[0]);}).map(function(e){return e[1];}),d=U.getContainer();"
           "d.textContent=t.length?'Depths: '+t.join('; '):'';d.style.display=t.length?'':'none';};"
           "m.on('layeradd layerremove',m.gobyShowUnits);m.gobyShowUnits();}";
}

std::string goby::apps::zeromq::LiaisonMap::tile_layers_js() const
{
    if (map_cfg_.blank_background())
        return "";

    const auto layers =
        map_cfg_.tile_layer_size() > 0 ? map_cfg_.tile_layer() : default_tile_layers();

    Wt::Json::Array specs;
    for (const auto& layer : layers)
    {
        Wt::Json::Object options;
        options["attribution"] = Wt::Json::Value(Wt::WString::fromUTF8(layer.attribution()));
        options["minZoom"] = Wt::Json::Value(layer.min_zoom());
        options["maxZoom"] = Wt::Json::Value(layer.max_zoom());
        if (layer.has_max_native_zoom())
            options["maxNativeZoom"] = Wt::Json::Value(layer.max_native_zoom());
        options["opacity"] = Wt::Json::Value(layer.opacity());
        if (layer.has_wms())
        {
            options["layers"] = Wt::Json::Value(Wt::WString::fromUTF8(layer.wms().layers()));
            options["format"] = Wt::Json::Value(Wt::WString::fromUTF8(layer.wms().format()));
            options["version"] = Wt::Json::Value(Wt::WString::fromUTF8(layer.wms().version()));
            options["styles"] = Wt::Json::Value(Wt::WString::fromUTF8(layer.wms().styles()));
            options["transparent"] = Wt::Json::Value(layer.overlay());
            options["tileSize"] = Wt::Json::Value(layer.tile_size());
        }

        Wt::Json::Object spec;
        spec["name"] = Wt::Json::Value(Wt::WString::fromUTF8(layer_name(layer)));
        spec["url"] = Wt::Json::Value(Wt::WString::fromUTF8(layer.url_template()));
        spec["wms"] = Wt::Json::Value(layer.has_wms());
        spec["overlay"] = Wt::Json::Value(layer.overlay());
        spec["shown"] = Wt::Json::Value(layer.shown());
        spec["depthUnits"] = Wt::Json::Value(Wt::WString::fromUTF8(layer.depth_units()));
        spec["options"] = Wt::Json::Value(std::move(options));
        specs.push_back(Wt::Json::Value(std::move(spec)));
    }

    // Wt's own tile layers cannot be removed, so the layers and Leaflet's layer control are made
    // here
    return "if(!m.gobyTileLayers){m.gobyTileLayers=1;var S=" + Wt::Json::serialize(specs, 0) +
           ",first=null;"
           "S.forEach(function(s,i){var o=s.options;o.zIndex=s.overlay?10+i:1;"
           "var l=s.wms?L.tileLayer.wms(s.url,o):L.tileLayer(s.url,o);"
           "if(s.depthUnits)m.gobyUnitLayers.push([l,s.name+' in '+s.depthUnits]);"
           "if(!s.overlay){m.gobyBases[s.name]=l;if(first===null)first=s.name;}});"
           "if(Object.keys(m.gobyBases).length>1)m.gobyControl();"
           "var b=m.gobySaved.base in m.gobyBases?m.gobySaved.base:first;"
           "if(b!==null)m.gobyBases[b].addTo(m);"
           "S.forEach(function(s,i){if(s.overlay){var o=s.options;o.zIndex=10+i;"
           "var l=s.wms?L.tileLayer.wms(s.url,o):L.tileLayer(s.url,o);"
           "if(s.depthUnits)m.gobyUnitLayers.push([l,s.name+' in '+s.depthUnits]);"
           "m.gobyAddOverlay(l,s.name,s.shown);}});"
           "m.gobyShowUnits();}";
}

void goby::apps::zeromq::LiaisonMap::update_map()
{
    if (!map_)
        return;

    const auto now = std::chrono::steady_clock::now();
    const auto stale_timeout = std::chrono::duration_cast<std::chrono::steady_clock::duration>(
        std::chrono::duration<double>(map_cfg_.stale_timeout_seconds()));

    std::string tracks;
    for (auto& [name, vehicle] : vehicles_)
    {
        if (!vehicle.marker)
            add_vehicle_ui(name, vehicle);

        if (name == followed_ && vehicle.fix_changed)
            map_->panTo(Wt::WLeafletMap::Coordinate(vehicle.fix.lat, vehicle.fix.lon));

        const auto age = now - vehicle.received;
        update_vehicle_ui(name, vehicle, age > stale_timeout, age);
        tracks += track_js(name, vehicle);
    }

    // points age out of the trail with no new data, so trim about once a second
    if (trail_seconds() > 0 && now - last_trail_age_ >= std::chrono::seconds(1))
    {
        last_trail_age_ = now;
        tracks += "m.gobyTrail(" + std::to_string(trail_seconds()) + ",false);";
    }

    if (!fitted_ && map_cfg_.fit_on_first_fix() && !vehicles_.empty())
    {
        fitted_ = true;
        fit_all();
    }

    if (!tracks.empty())
        run_map_js(tracks);
}

void goby::apps::zeromq::LiaisonMap::add_vehicle_ui(const std::string& name, Vehicle& vehicle)
{
    auto widget = std::make_unique<Wt::WContainerWidget>();
    widget->setStyleClass("map-vehicle");
    vehicle.marker_widget = widget.get();
    vehicle.arrow =
        widget->addNew<Wt::WText>(arrow_svg(vehicle.fix.heading, vehicle_colour(vehicle.fix.type)),
                                  Wt::TextFormat::UnsafeXHTML);
    vehicle.label = widget->addNew<Wt::WText>(Wt::WString::fromUTF8(name), Wt::TextFormat::Plain);
    vehicle.label->setStyleClass("map-vehicle-label");
    vehicle.depth_gauge = widget->addNew<Wt::WText>("", Wt::TextFormat::UnsafeXHTML);
    vehicle.depth_gauge->setStyleClass("map-depth-gauge");
    vehicle.depth_gauge->setHidden(true);

    auto marker = std::make_unique<Wt::WLeafletMap::WidgetMarker>(
        Wt::WLeafletMap::Coordinate(vehicle.fix.lat, vehicle.fix.lon), std::move(widget));
    marker->setAnchorPoint(12, 12);
    vehicle.marker = map_->addMarker(std::move(marker));

    // every vehicle sorted before this one already has a row
    const int index = std::distance(vehicles_.begin(), vehicles_.find(name));
    vehicle.row = vehicle_table_->insertRow(index + vehicle_table_->headerCount());
    for (int column = 0; column < Vehicle::COLUMN_COUNT; ++column)
    {
        auto* cell = vehicle.row->elementAt(column);
        vehicle.cells[column] = cell->addNew<Wt::WText>();
        cell->setHidden(!column_shown_[column]);
        cell->clicked().connect([this, name]() { centre_on(name); });
    }
    vehicle.cells[Vehicle::COLUMN_NAME]->setText(Wt::WString::fromUTF8(name));
    vehicle.row->setStyleClass(row_style(name, vehicle));

    follow_box_->insertItem(index + 1, Wt::WString::fromUTF8(name));
    waiting_text_->hide();
    vehicle.fix_changed = true;
}

void goby::apps::zeromq::LiaisonMap::show_column(int column)
{
    column_shown_[column] = true;
    for (int row = 0; row < vehicle_table_->rowCount(); ++row)
        vehicle_table_->elementAt(row, column)->setHidden(false);
}

void goby::apps::zeromq::LiaisonMap::update_vehicle_ui(const std::string& name, Vehicle& vehicle,
                                                       bool stale,
                                                       std::chrono::steady_clock::duration age)
{
    if (vehicle.fix_changed || stale != vehicle.stale)
    {
        const auto& fix = vehicle.fix;
        vehicle.stale = stale;

        if (vehicle.fix_changed)
            vehicle.marker->move(Wt::WLeafletMap::Coordinate(fix.lat, fix.lon));
        vehicle.arrow->setText(arrow_svg(fix.heading, vehicle_colour(fix.type)));
        if (has_depth_gauge(fix))
            vehicle.depth_gauge->setText(depth_gauge_svg(fix.depth));
        vehicle.depth_gauge->setHidden(!show_depth_ || !has_depth_gauge(fix));
        vehicle.marker_widget->toggleStyleClass("stale", stale);
        vehicle.row->setStyleClass(row_style(name, vehicle));

        vehicle.cells[Vehicle::COLUMN_HEADING]->setText(format_heading(fix.heading));
        vehicle.cells[Vehicle::COLUMN_SPEED]->setText(format_number("%.1f", fix.speed));
        vehicle.cells[Vehicle::COLUMN_DEPTH]->setText(format_number("%.1f", fix.depth));
        vehicle.cells[Vehicle::COLUMN_LAT]->setText(format_number("%.6f", fix.lat));
        vehicle.cells[Vehicle::COLUMN_LON]->setText(format_number("%.6f", fix.lon));
        for (auto [column, value] :
             {std::make_pair(Vehicle::COLUMN_PITCH, fix.pitch),
              std::make_pair(Vehicle::COLUMN_ROLL, fix.roll),
              std::make_pair(Vehicle::COLUMN_ALTITUDE, fix.altitude),
              std::make_pair(Vehicle::COLUMN_X, fix.x), std::make_pair(Vehicle::COLUMN_Y, fix.y)})
        {
            vehicle.cells[column]->setText(format_number("%.1f", value));
            if (!std::isnan(value) && !column_shown_[column])
                show_column(column);
        }
        vehicle.cells[Vehicle::COLUMN_NAME]->setToolTip(Wt::WString::fromUTF8(
            goby::middleware::frontseat::protobuf::VehicleType_Name(fix.type) + " " +
            format_number("%.6f", fix.lat) + ", " + format_number("%.6f", fix.lon)));

        vehicle.fix_changed = false;
    }
    vehicle.cells[Vehicle::COLUMN_AGE]->setText(format_age(age));
    vehicle.label->setText(Wt::WString::fromUTF8(label_text(name, vehicle, age)));
}

std::string
goby::apps::zeromq::LiaisonMap::label_text(const std::string& name, const Vehicle& vehicle,
                                           std::chrono::steady_clock::duration age) const
{
    return (show_age_ || vehicle.stale) ? name + " \u00b7 " + format_age(age) : name;
}

std::string goby::apps::zeromq::LiaisonMap::row_style(const std::string& name,
                                                      const Vehicle& vehicle) const
{
    std::string style = "map-vehicle-row";
    if (vehicle.stale)
        style += " stale";
    if (name == followed_)
        style += " followed";
    return style;
}

std::string goby::apps::zeromq::LiaisonMap::track_js(const std::string& name, Vehicle& vehicle)
{
    if (vehicle.unsent_track.empty())
        return "";

    const auto now = std::chrono::steady_clock::now();
    std::ostringstream js;
    js << std::fixed << std::setprecision(7) << "m.gobyTrack("
       << Wt::WWebWidget::jsStringLiteral(name) << ",'" << vehicle_colour(vehicle.fix.type)
       << "',[";
    for (auto it = vehicle.unsent_track.begin(); it != vehicle.unsent_track.end(); ++it)
    {
        const auto age_ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(now - it->received).count();
        js << (it == vehicle.unsent_track.begin() ? "" : ",") << "[" << it->position.first << ","
           << it->position.second << "," << age_ms << "]";
    }
    js << "]," << map_cfg_.track_length() << "," << trail_seconds() << ");";

    vehicle.unsent_track.clear();
    return js.str();
}

void goby::apps::zeromq::LiaisonMap::run_map_js(const std::string& js)
{
    // the Leaflet map object can be recreated by Wt, so the helpers are installed on demand
    Wt::WApplication::instance()->doJavaScript("{var m=" + map_->mapJsRef() + ";if(m){" +
                                               js_setup_ + js + "}}");
}

void goby::apps::zeromq::LiaisonMap::update_timer()
{
    if (map_ && focused_ && !paused_)
    {
        if (!map_timer_.isActive())
            map_timer_.start();
    }
    else
    {
        map_timer_.stop();
    }
}

void goby::apps::zeromq::LiaisonMap::centre_on(const std::string& name)
{
    auto it = vehicles_.find(name);
    if (!map_ || it == vehicles_.end())
        return;

    if (!followed_.empty() && followed_ != name)
        set_following("");
    map_->panTo(Wt::WLeafletMap::Coordinate(it->second.fix.lat, it->second.fix.lon));
}

void goby::apps::zeromq::LiaisonMap::fit_all()
{
    if (!map_ || vehicles_.empty())
        return;

    const bool all_stale = std::all_of(vehicles_.begin(), vehicles_.end(),
                                       [](const auto& v) { return v.second.stale; });

    double south = 90, north = -90, west = 180, east = -180;
    for (const auto& [name, vehicle] : vehicles_)
    {
        if (vehicle.stale && !all_stale)
            continue;
        south = std::min(south, vehicle.fix.lat);
        north = std::max(north, vehicle.fix.lat);
        west = std::min(west, vehicle.fix.lon);
        east = std::max(east, vehicle.fix.lon);
    }

    set_following("");

    std::ostringstream js;
    js << std::fixed << std::setprecision(7) << "m.fitBounds([[" << south << "," << west << "],["
       << north << "," << east << "]],{padding:[40,40],maxZoom:16});";
    run_map_js(js.str());
}

void goby::apps::zeromq::LiaisonMap::clear_tracks()
{
    for (auto& [name, vehicle] : vehicles_)
    {
        vehicle.unsent_track.clear();
        vehicle.has_track_point = false;
    }
    if (map_)
        run_map_js("m.gobyClearTracks();");
}

void goby::apps::zeromq::LiaisonMap::set_following(const std::string& name)
{
    followed_ = name;

    if (follow_box_)
    {
        const int index = name.empty() ? 0 : follow_box_->findText(Wt::WString::fromUTF8(name));
        if (index >= 0 && follow_box_->currentIndex() != index)
            follow_box_->setCurrentIndex(index);
    }

    for (auto& [vehicle_name, vehicle] : vehicles_)
    {
        if (vehicle.row)
            vehicle.row->setStyleClass(row_style(vehicle_name, vehicle));
    }

    auto it = vehicles_.find(name);
    if (map_ && it != vehicles_.end())
        map_->panTo(Wt::WLeafletMap::Coordinate(it->second.fix.lat, it->second.fix.lon));
}

void goby::apps::zeromq::LiaisonMap::toggle_pause()
{
    paused_ = !paused_;
    pause_button_->setText(paused_ ? "Resume" : "Pause");
    update_timer();
    if (!paused_)
        update_map();
}

void goby::apps::zeromq::LiaisonMap::set_trail(int step)
{
    trail_step_ = step;
    trail_text_->setText(trail_label(trail_seconds()));
    if (map_)
        run_map_js("m.gobyTrail(" + std::to_string(trail_seconds()) + ",true);");
}

int goby::apps::zeromq::LiaisonMap::trail_seconds() const { return trail_steps.at(trail_step_); }

goby::apps::zeromq::MapCommsThread::MapCommsThread(LiaisonMap* map,
                                                   const protobuf::LiaisonConfig& config, int index)
    : LiaisonCommsThread<LiaisonMap>(map, config, index), map_(map)
{
    if (config.pb_map_config().source_size() == 0)
    {
        Source node_status;
        node_status.set_kind(Source::NODE_STATUS);
        subscribe(node_status);
    }
    for (const auto& source : config.pb_map_config().source()) subscribe(source);
}

void goby::apps::zeromq::MapCommsThread::subscribe(const Source& source)
{
    using goby::middleware::frontseat::protobuf::NodeStatus;
    using goby::middleware::protobuf::gpsd::TimePositionVelocity;

    const std::string default_name =
        source.has_name_override() ? source.name_override() : cfg().interprocess().platform();

    auto post = [map = map_](const VehicleFix& fix)
    { map->post_to_wt([map, fix]() { map->handle_fix(fix); }); };

    std::string type, group;
    std::function<void(const std::vector<unsigned char>&)> parse;
    switch (source.kind())
    {
        case Source::NODE_STATUS:
            type = NodeStatus::descriptor()->full_name();
            group = std::string(goby::middleware::frontseat::groups::node_status);
            parse = [post, default_name](const std::vector<unsigned char>& data)
            {
                NodeStatus status;
                if (!status.ParseFromArray(data.data(), data.size()))
                    return;

                VehicleFix fix;
                fix.name = status.name().empty() ? default_name : status.name();
                fix.lat = status.global_fix().lat();
                fix.lon = status.global_fix().lon();
                fix.depth = status.global_fix().depth();
                if (status.global_fix().has_altitude())
                    fix.altitude = status.global_fix().altitude();
                if (status.has_local_fix())
                {
                    fix.x = status.local_fix().x();
                    fix.y = status.local_fix().y();
                }
                if (status.pose().has_heading())
                    fix.heading = status.pose().heading();
                if (status.pose().has_pitch())
                    fix.pitch = status.pose().pitch();
                if (status.pose().has_roll())
                    fix.roll = status.pose().roll();
                fix.speed = status.speed().over_ground();
                fix.type = status.type();
                post(fix);
            };
            break;

        case Source::GPSD_TPV:
            type = TimePositionVelocity::descriptor()->full_name();
            group = std::string(goby::middleware::groups::gpsd::tpv);
            parse = [post, default_name](const std::vector<unsigned char>& data)
            {
                TimePositionVelocity tpv;
                if (!tpv.ParseFromArray(data.data(), data.size()) || !tpv.has_location() ||
                    (tpv.has_mode() && tpv.mode() < TimePositionVelocity::Mode2D))
                    return;

                VehicleFix fix;
                fix.name = default_name;
                fix.lat = tpv.location().lat();
                fix.lon = tpv.location().lon();
                if (tpv.has_track())
                    fix.heading = tpv.track();
                if (tpv.has_speed())
                    fix.speed = tpv.speed();
                post(fix);
            };
            break;
    }

    subscriptions_.push_back(interprocess().subscribe_regex(
        [parse](const std::vector<unsigned char>& data, int /*scheme*/, const std::string& /*type*/,
                const goby::middleware::Group& /*group*/) { parse(data); },
        {goby::middleware::MarshallingScheme::PROTOBUF}, exact_regex(type),
        source.has_group_regex() ? source.group_regex() : exact_regex(group)));
}
