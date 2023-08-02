// Copyright 2020-2023:
//   GobySoft, LLC (2013-)
//   Community contributors (see AUTHORS file)
// File authors:
//   Shawn Dooley <shawn@shawndooley.net>
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

#include <algorithm>        // for copy
#include <exception>        // for exception
#include <initializer_list> // for initia...
#include <iostream>         // for basic_...
#include <map>              // for operat...
#include <unordered_map>    // for operat...
#include <vector>           // for vector

#include <boost/algorithm/string/classification.hpp> // for is_any...
#include <boost/algorithm/string/trim.hpp>           // for trim
#include <boost/date_time/gregorian/gregorian.hpp>
#include <boost/date_time/posix_time/ptime.hpp>        // for ptime
#include <boost/date_time/posix_time/time_parsers.hpp> // for from_i...
#include <boost/range/algorithm_ext/erase.hpp>         // for remove...
#include <boost/units/operators.hpp>                   // for units
#include <boost/units/quantity.hpp>                    // for operator*
#include <boost/units/systems/angle/degrees.hpp>       // for plane_...
#include <boost/units/systems/si/length.hpp>           // for length
#include <boost/units/systems/si/time.hpp>             // for second
#include <boost/units/unit.hpp>                        // for operator/

#include "goby/middleware/marshalling/protobuf.h"

#include "goby/middleware/application/configuration_reader.h" // for Config...
#include "goby/middleware/application/interface.h"            // for run
#include "goby/middleware/gpsd/groups.h"                      // for att, sky
#include "goby/middleware/io/line_based/tcp_client.h"
#include "goby/middleware/protobuf/geographic.pb.h" // for LatLon...
#include "goby/middleware/protobuf/gpsd.pb.h"       // for TimePo...
#include "goby/time/convert.h"                      // for convert
#include "goby/time/types.h"                        // for SITime
#include "goby/util/debug_logger/flex_ostream.h"    // for operat...
#include "goby/util/thirdparty/nlohmann/json.hpp"   // for json
#include "goby/zeromq/protobuf/gps_config.pb.h"     // for GPSDCo...
#include "goby/zeromq/transport/interprocess.h"     // for InterP...

#include "gpsd_client.h"

using goby::glog;

constexpr goby::middleware::Group tcp_in{"tcp_in"};
constexpr goby::middleware::Group tcp_out{"tcp_out"};

namespace
{
// Minor tweak to handle the time format from GPSD;
goby::time::SITime parse_time(std::string s)
{
    // For reference.
    // https://gpsd.io/gpsd_json.html
    // From Documentation:
    // Time/date stamp in ISO8601 format, UTC. May have a fractional part of up to .001sec precision. May be absent if the mode is not 2D or 3D.

    // Convert from extended to basic format by removing ':' and '-' characters
    // Remove the "Z" from the end of the string because it messes up the
    // parser.
    boost::remove_erase_if(s, boost::is_any_of(":-Z"));
    boost::posix_time::ptime t(boost::posix_time::from_iso_string(s));

    return goby::time::convert<goby::time::SITime>(t);
}
} // end of Anonymous namespace

class GPSDClientConfigurator
    : public goby::middleware::ProtobufConfigurator<goby::apps::zeromq::protobuf::GPSDConfig>
{
  public:
    GPSDClientConfigurator(int argc, char* argv[])
        : goby::middleware::ProtobufConfigurator<goby::apps::zeromq::protobuf::GPSDConfig>(argc,
                                                                                           argv)
    {
        auto& cfg = mutable_cfg();
        auto& gpsd = *cfg.mutable_gpsd();
        if (!gpsd.has_remote_address())
            gpsd.set_remote_address("127.0.0.1");
        if (!gpsd.has_remote_port())
            gpsd.set_remote_port(2947);
    }
};

goby::apps::zeromq::GPSDClient::GPSDClient()
{
    if (cfg().device_name_size())
    {
        for (auto& d_name : cfg().device_name()) { device_list_.insert(d_name); }
    }
    else
    {
        publish_all_ = true;

        glog.is_warn() && glog << "No device configured. We will publish all GPS data."
                               << std::endl;
    }

    using TCPThread = goby::middleware::io::TCPClientThreadLineBased<
        tcp_in, tcp_out, goby::middleware::io::PubSubLayer::INTERTHREAD,
        goby::middleware::io::PubSubLayer::INTERTHREAD>;

    interthread().subscribe<tcp_in>([this](const goby::middleware::protobuf::IOData& data) {
        try
        {
            auto json_data = nlohmann::json::parse(data.data());
            handle_response(json_data);
        }
        catch (const std::exception& e)
        {
            glog.is_warn() && glog << "Exception parsing incoming data: " << e.what() << std::endl;
        }
    });

    interthread().subscribe<tcp_in>(
        [this](const goby::middleware::protobuf::TCPClientEvent& event) {
            if (event.event() == goby::middleware::protobuf::TCPClientEvent::EVENT_CONNECT)
            {
                glog.is_debug1() && glog << "Received CONNECT event, sending WATCH" << std::endl;
                goby::middleware::protobuf::IOData cmd;
                nlohmann::json watch_params;
                watch_params["class"] = "WATCH";
                watch_params["enable"] = true;
                watch_params["json"] = true;
                watch_params["nmea"] = false;
                watch_params["raw"] = 0;
                watch_params["scaled"] = false;
                watch_params["split24"] = false;
                watch_params["pps"] = false;

                if (device_list_.size() == 1)
                    watch_params["device"] = *device_list_.begin();

                cmd.set_data("?WATCH=" + watch_params.dump());
                interthread().publish<tcp_out>(cmd);
            }
        });

    // launch after subscribing
    launch_thread<TCPThread>(cfg().gpsd());
}

void goby::apps::zeromq::GPSDClient::handle_response(nlohmann::json& json_data)
{
    if (json_data.contains("class"))
    {
        auto& j_class = json_data["class"];
        if (j_class == "TPV")
            handle_tpv(json_data);
        else if (j_class == "SKY")
            handle_sky(json_data);
        else if (j_class == "ATT")
            handle_att(json_data);
        else if (j_class == "ERROR")
            glog.is_warn() && glog << "GPSD returns error: " << json_data.dump() << std::endl;
    }
}

void goby::apps::zeromq::GPSDClient::handle_tpv(nlohmann::json& data)
{
    bool device_in_config = device_list_.count(data["device"]);
    bool device_in_data = data.contains("device");

    if ((device_in_config && device_in_data) || publish_all_)
    {
        middleware::protobuf::gpsd::TimePositionVelocity tpv;
        if (device_in_data)
            tpv.set_device(data["device"]);

        using namespace boost::units;

        if (data.contains("time"))
            tpv.set_time_with_units(parse_time(data["time"]));

        if (data.contains("lat") && data.contains("lon"))
        {
            auto loc = tpv.mutable_location();
            loc->set_lat_with_units(data["lat"].get<double>() * degree::degree);
            loc->set_lon_with_units(data["lon"].get<double>() * degree::degree);
        }

        if (data.contains("mode"))
        {
            using namespace middleware::protobuf::gpsd;
            int mode = data["mode"].get<int>();
            if (mode == 0)
                tpv.set_mode(TimePositionVelocity::ModeNotSeen);
            else if (mode == 1)
                tpv.set_mode(TimePositionVelocity::ModeNoFix);
            else if (mode == 2)
                tpv.set_mode(TimePositionVelocity::Mode2D);
            else if (mode == 3)
                tpv.set_mode(TimePositionVelocity::Mode3D);
        }

        if (data.contains("speed"))
            tpv.set_speed_with_units(data["speed"].get<double>() * (si::meters / si::second));

        if (data.contains("alt"))
            tpv.set_altitude_with_units(data["alt"].get<double>() * si::meter);

        if (data.contains("climb"))
            tpv.set_climb_with_units(data["climb"].get<double>() * (si::meters / si::second));

        if (data.contains("track"))
            tpv.set_track_with_units(data["track"].get<double>() * degree::degree);

        if (data.contains("epc"))
            tpv.set_epc_with_units(data["epc"].get<double>() * (si::meters / si::second));

        if (data.contains("epd"))
            tpv.set_epd_with_units(data["epd"].get<double>() * degree::degree);

        if (data.contains("eps"))
            tpv.set_eps_with_units(data["eps"].get<double>() * (si::meters / si::seconds));

        if (data.contains("ept"))
            tpv.set_ept_with_units(data["ept"].get<double>() * si::seconds);

        if (data.contains("epv"))
            tpv.set_epv_with_units(data["epv"].get<double>() * si::meter);

        if (data.contains("epx"))
            tpv.set_epx_with_units(data["epx"].get<double>() * si::meter);

        if (data.contains("epy"))
            tpv.set_epy_with_units(data["epy"].get<double>() * si::meter);

        interprocess().publish<goby::middleware::groups::gpsd::tpv>(tpv);
        glog.is_debug1() && glog << "TPV: " << tpv.ShortDebugString() << std::endl;
    }
}

void goby::apps::zeromq::GPSDClient::handle_sky(nlohmann::json& data)
{
    bool device_in_config = device_list_.count(data["device"]);
    bool device_in_data = data.contains("device");

    if ((device_in_config && device_in_data) || publish_all_)
    {
        middleware::protobuf::gpsd::SkyView sky;
        if (device_in_data)
            sky.set_device(data["device"]);

        using namespace boost::units;

        if (data.contains("time"))
            sky.set_time_with_units(parse_time(data["time"]));

        if (data.contains("gdop"))
            sky.set_gdop((data["gdop"].get<double>()));

        if (data.contains("hdop"))
            sky.set_hdop((data["hdop"].get<double>()));

        if (data.contains("pdop"))
            sky.set_pdop((data["pdop"].get<double>()));

        if (data.contains("tdop"))
            sky.set_tdop((data["tdop"].get<double>()));

        if (data.contains("vdop"))
            sky.set_vdop((data["vdop"].get<double>()));

        if (data.contains("xdop"))
            sky.set_xdop((data["xdop"].get<double>()));

        if (data.contains("vdop"))
            sky.set_vdop((data["vdop"].get<double>()));

        if (data.contains("nSat"))
            sky.set_nsat((data["nSat"].get<int>()));

        if (data.contains("uSat"))
            sky.set_usat((data["uSat"].get<int>()));

        int usat = 0;
        if (data.contains("satellites"))
        {
            auto& satellites = data["satellites"];

            for (auto& sat : satellites)
            {
                auto* sat_pb = sky.add_satellite();

                if (sat.contains("PRN"))
                    sat_pb->set_prn(sat["PRN"].get<int>());

                if (sat.contains("az"))
                    sat_pb->set_az_with_units(sat["az"].get<double>() * degree::degrees);

                if (sat.contains("el"))
                    sat_pb->set_el_with_units(sat["el"].get<double>() * degree::degrees);

                if (sat.contains("ss"))
                    sat_pb->set_ss(sat["ss"].get<double>());

                if (sat.contains("used"))
                    sat_pb->set_used(sat["used"].get<bool>());

                if (sat.contains("gnssid"))
                    sat_pb->set_gnssid(sat["gnssid"].get<int>());

                if (sat.contains("svid"))
                    sat_pb->set_svid(sat["svid"].get<int>());

                if (sat.contains("sigid"))
                    sat_pb->set_sigid(sat["sigid"].get<int>());

                if (sat.contains("freqid"))
                    sat_pb->set_freqid(sat["freqid"].get<int>());

                if (sat.contains("health"))
                    sat_pb->set_health(sat["health"].get<int>());

                if (sat_pb->used())
                    ++usat;
            }
        }
        sky.set_nsat(sky.satellite_size());
        sky.set_usat(usat);
        
        interprocess().publish<goby::middleware::groups::gpsd::sky>(sky);
        glog.is_debug1() && glog << "SKY: " << sky.ShortDebugString() << std::endl;
    }
}

void goby::apps::zeromq::GPSDClient::handle_att(nlohmann::json& data)
{
    bool device_in_config = device_list_.count(data["device"]);
    bool device_in_data = data.contains("device");

    if ((device_in_config && device_in_data) || publish_all_)
    {
        middleware::protobuf::gpsd::Attitude att;
        if (device_in_data)
            att.set_device(data["device"]);

        using namespace boost::units;

        if (data.contains("time"))
            att.set_time_with_units(parse_time(data["time"]));

        if (data.contains("heading"))
            att.set_heading_with_units(data["heading"].get<double>() * degree::degree);

        if (data.contains("yaw"))
            att.set_yaw_with_units(data["yaw"].get<double>() * degree::degree);

        if (data.contains("pitch"))
            att.set_pitch_with_units(data["pitch"].get<double>() * degree::degree);
        if (data.contains("roll"))
            att.set_roll_with_units(data["roll"].get<double>() * degree::degree);

        interprocess().publish<goby::middleware::groups::gpsd::att>(att);
        glog.is_debug1() && glog << "ATT: " << att.ShortDebugString() << std::endl;
    }
}

int main(int argc, char* argv[])
{
    return goby::run<goby::apps::zeromq::GPSDClient>(GPSDClientConfigurator(argc, argv));
}
