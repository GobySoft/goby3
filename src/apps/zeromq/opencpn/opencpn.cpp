// Copyright 2021:
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

#include "goby/middleware/marshalling/protobuf.h"

#include "goby/middleware/ais.h"
#include "goby/middleware/frontseat/groups.h"
#include "goby/middleware/io/line_based/pty.h"
#include "goby/middleware/io/line_based/tcp_server.h"
#include "goby/middleware/opencpn/groups.h"
#include "goby/time/convert.h"
#include "goby/time/system_clock.h"
#include "goby/util/ais.h"
#include "goby/util/linebasedcomms/gps_sentence.h"
#include "goby/zeromq/application/multi_thread.h"
#include "goby/zeromq/protobuf/opencpn_config.pb.h"

using goby::glog;
namespace si = boost::units::si;
using ApplicationBase =
    goby::zeromq::MultiThreadApplication<goby::apps::zeromq::protobuf::OpenCPNInterfaceConfig>;

namespace goby
{
namespace apps
{
namespace zeromq
{
class OpenCPNInterface : public ApplicationBase
{
  public:
    using TCPServerThread = goby::middleware::io::TCPServerThreadLineBased<
        goby::middleware::io::groups::nmea0183_in, goby::middleware::io::groups::nmea0183_out,
        goby::middleware::io::PubSubLayer::INTERTHREAD,
        goby::middleware::io::PubSubLayer::INTERTHREAD>;
    using PTYThread =
        goby::middleware::io::PTYThreadLineBased<goby::middleware::io::groups::nmea0183_in,
                                                 goby::middleware::io::groups::nmea0183_out,
                                                 goby::middleware::io::PubSubLayer::INTERTHREAD,
                                                 goby::middleware::io::PubSubLayer::INTERTHREAD>;

    OpenCPNInterface();

  private:
    void handle_status(const goby::middleware::frontseat::protobuf::NodeStatus& frontseat_nav);
    void handle_nmea_from_ocpn(const goby::util::NMEASentence& nmea);

  private:
    // vehicle name to data
    struct VehicleData
    {
        goby::middleware::AISConverter converter;
        goby::time::SystemClock::time_point last_ais_pos_t{std::chrono::seconds(0)},
            last_ais_voy_t{std::chrono::seconds(0)};
    };

    std::map<std::string, VehicleData> vehicles_;
    int next_mmsi_;

    // existing waypoints from WPL sentences
    std::map<std::string, goby::util::gps::WPL> waypoints_;
    // store individual RTE message (fragments) until we have a full route
    std::multimap<std::string, goby::util::gps::RTE> route_fragments_;
    goby::middleware::protobuf::Waypoint last_waypoint_;

    goby::time::SystemClock::duration ais_pos_dt_, ais_voy_dt_;
};
} // namespace zeromq
} // namespace apps
} // namespace goby

int main(int argc, char* argv[])
{
    return goby::run<goby::apps::zeromq::OpenCPNInterface>(argc, argv);
}

goby::apps::zeromq::OpenCPNInterface::OpenCPNInterface()
    : next_mmsi_(cfg().mmsi_start()),
      ais_pos_dt_(goby::time::convert_duration<decltype(ais_pos_dt_)>(
          cfg().position_report_interval_with_units())),
      ais_voy_dt_(goby::time::convert_duration<decltype(ais_voy_dt_)>(
          cfg().voyage_report_interval_with_units()))
{
    interprocess().subscribe<goby::middleware::frontseat::groups::node_status>(
        [this](const goby::middleware::frontseat::protobuf::NodeStatus& frontseat_nav) {
            handle_status(frontseat_nav);
        });

    interthread().subscribe<goby::middleware::io::groups::nmea0183_in>(
        [this](const goby::middleware::protobuf::IOData& io_data) {
            try
            {
                goby::util::NMEASentence nmea(io_data.data());
                handle_nmea_from_ocpn(nmea);
            }
            catch (goby::util::bad_nmea_sentence& e)
            {
                glog.is_warn() && glog << "Ignoring invalid NMEA sentence: "
                                       << io_data.ShortDebugString() << std::endl;
            }
        });

    if (cfg().has_ais_server())
        launch_thread<TCPServerThread>(cfg().ais_server());
    else if (cfg().has_ais_serial())
        launch_thread<PTYThread>(cfg().ais_serial());
}

void goby::apps::zeromq::OpenCPNInterface::handle_status(
    const goby::middleware::frontseat::protobuf::NodeStatus& frontseat_nav)
{
    if (!vehicles_.count(frontseat_nav.name()))
        vehicles_.insert(std::make_pair(
            frontseat_nav.name(),
            VehicleData({goby::middleware::AISConverter(next_mmsi_++, cfg().filter_length())})));

    auto& vehicle_data = vehicles_.at(frontseat_nav.name());
    auto& converter = vehicle_data.converter;

    converter.add_status(frontseat_nav);
    std::pair<goby::util::ais::protobuf::Position, goby::util::ais::protobuf::Voyage> ais_b_msg =
        converter.latest_node_status_to_ais_b();

    auto now = goby::time::SystemClock::now();
    bool write_pos = (now > vehicle_data.last_ais_pos_t + ais_pos_dt_);
    bool write_voy = (now > vehicle_data.last_ais_voy_t + ais_voy_dt_);

    std::vector<goby::util::NMEASentence> nmeas;
    if (write_pos)
    {
        goby::util::ais::Encoder pos_encoder(ais_b_msg.first);
        nmeas = pos_encoder.as_nmea();
        vehicle_data.last_ais_pos_t = now;
    }
    if (write_voy)
    {
        goby::util::ais::Encoder voy_encoder_part0(ais_b_msg.second, 0);
        goby::util::ais::Encoder voy_encoder_part1(ais_b_msg.second, 1);
        auto nmeas_0 = voy_encoder_part0.as_nmea();
        std::copy(nmeas_0.begin(), nmeas_0.end(), std::back_inserter(nmeas));
        auto nmeas_1 = voy_encoder_part1.as_nmea();
        std::copy(nmeas_1.begin(), nmeas_1.end(), std::back_inserter(nmeas));
        vehicle_data.last_ais_voy_t = now;
    }

    for (auto nmea : nmeas)
    {
        glog.is_debug1() && glog << nmea.message() << std::endl;
        goby::middleware::protobuf::IOData io_data;
        io_data.set_data(nmea.message_cr_nl());
        if (cfg().has_ais_server())
            io_data.mutable_tcp_dest()->set_all_clients(true);
        interthread().publish<goby::middleware::io::groups::nmea0183_out>(io_data);
    }
}

void goby::apps::zeromq::OpenCPNInterface::handle_nmea_from_ocpn(
    const goby::util::NMEASentence& nmea)
{
    auto to_pb_waypoint = [](goby::util::gps::WPL wpl) -> goby::middleware::protobuf::Waypoint {
        goby::middleware::protobuf::Waypoint pb_waypoint;
        if (wpl.name)
            pb_waypoint.set_name(wpl.name.get());
        if (wpl.latitude)
            pb_waypoint.mutable_location()->set_lat_with_units(wpl.latitude.get());
        if (wpl.longitude)
            pb_waypoint.mutable_location()->set_lon_with_units(wpl.longitude.get());
        return pb_waypoint;
    };

    if (nmea.sentence_id() == "WPL")
    {
        glog.is_debug1() && glog << "Received WPL: " << nmea.message() << std::endl;
        goby::util::gps::WPL wpl(nmea);

        if (wpl.name)
            waypoints_[wpl.name.get()] = wpl;

        auto pb_waypoint = to_pb_waypoint(wpl);

        // reject any duplicate WPL messages (OpenCPN sends them in pairs of two for some reason)
        if (!last_waypoint_.IsInitialized() ||
            pb_waypoint.SerializeAsString() != last_waypoint_.SerializeAsString())
        {
            glog.is_debug1() && glog << "Publishing waypoint: " << pb_waypoint.ShortDebugString()
                                     << std::endl;
            interprocess().publish<goby::middleware::groups::opencpn::waypoint>(pb_waypoint);
            last_waypoint_ = pb_waypoint;
        }
    }
    else if (nmea.sentence_id() == "RTE")
    {
        glog.is_debug1() && glog << "Received RTE: " << nmea.message() << std::endl;
        goby::util::gps::RTE rte(nmea);

        if (!rte.name || !rte.total_number_sentences || !rte.current_sentence_index)
        {
            glog.is_warn() && glog << "Missing required components in route message" << std::endl;
            return;
        }

        const auto& route_name = *rte.name;

        // route indexes at 1, so this is a new route
        if (*rte.current_sentence_index == 1)
            route_fragments_.erase(route_name);

        route_fragments_.insert(std::make_pair(route_name, rte));

        // should have a complete route now
        if (*rte.current_sentence_index == *rte.total_number_sentences)
        {
            glog.is_debug1() && glog << "Attempting to assemble route \"" << route_name << "\""
                                     << std::endl;
            // sort by sentence number
            std::map<int, goby::util::gps::RTE> route;
            for (const auto& route_fragment_pair : route_fragments_)
            {
                if (route_fragment_pair.first == route_name)
                    route.insert(std::make_pair(*route_fragment_pair.second.current_sentence_index,
                                                route_fragment_pair.second));
            }

            int sentence_number_expected = 1;
            goby::middleware::protobuf::Route pb_route;
            pb_route.set_name(route_name);

            try
            {
                if (route.size() != *rte.total_number_sentences)
                    throw(goby::Exception("wrong number of sentences for route, expected " +
                                          std::to_string(*rte.total_number_sentences) +
                                          ", received " + std::to_string(route.size())));

                for (const auto& route_pair : route)
                {
                    if (route_pair.first != sentence_number_expected)
                        throw(goby::Exception("missing sentence index: " +
                                              std::to_string(sentence_number_expected)));
                    for (const auto& waypoint_name : route_pair.second.waypoint_names)
                    {
                        if (!waypoints_.count(waypoint_name))
                            throw(goby::Exception("missing waypoint (WPL) for waypoint name \"" +
                                                  waypoint_name + "\""));

                        *pb_route.add_point() = to_pb_waypoint(waypoints_[waypoint_name]);
                    }
                    ++sentence_number_expected;
                }
                route_fragments_.erase(route_name);
                glog.is_debug1() && glog << "Publishing route: " << pb_route.ShortDebugString()
                                         << std::endl;
                interprocess().publish<goby::middleware::groups::opencpn::route>(pb_route);
            }
            catch (std::exception& e)
            {
                glog.is_warn() && glog << "Could not assemble route \"" << route_name
                                       << "\": " << e.what() << std::endl;
            }
        }
    }
}
