// Copyright 2013-2021:
//   GobySoft, LLC (2013-)
//   Massachusetts Institute of Technology (2007-2014)
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

//
// Usage:
// 1. run basic_frontseat_frontseat_simulator running on some port (as TCP server)
// > basic_frontseat_modem_simulator 54321
// 2. run goby_frontseat_interface or iFrontSeat connecting to that port

#include <cmath>       // for isnan, cos, sin
#include <cstdlib>     // for exit, abs
#include <iomanip>     // for operator<<, set...
#include <iostream>    // for operator<<, bas...
#include <limits>      // for numeric_limits
#include <map>         // for map, map<>::map...
#include <memory>      // for unique_ptr
#include <stdexcept>   // for runtime_error
#include <string>      // for string, basic_s...
#include <type_traits> // for __decay_and_str...
#include <unistd.h>    // for sleep, usleep
#include <utility>     // for pair, make_pair
#include <vector>      // for vector

#include <boost/algorithm/string/classification.hpp> // for is_any_ofF, is_...
#include <boost/algorithm/string/split.hpp>          // for split
#include <boost/algorithm/string/trim.hpp>           // for trim
#include <boost/units/quantity.hpp>                  // for operator*, quan...
#include <boost/units/systems/angle/degrees.hpp>     // for plane_angle
#include <boost/units/systems/si/length.hpp>         // for length, meters

#include "goby/util/as.h"                         // for as
#include "goby/util/constants.h"                  // for NaN, pi
#include "goby/util/geodesy.h"                    // for UTMGeodesy, UTM...
#include "goby/util/linebasedcomms/tcp_server.h"  // for TCPServer
#include "goby/util/protobuf/linebasedcomms.pb.h" // for Datagram

struct VehicleConfig
{
    // acceleration / deceleration
    double a = 0.5; // m/s^2

    // rate of heading change
    double hdg_rate = 45; // deg/s

    // rate of depth change (velocity in z)
    double z_rate = 2; // m/s
};

VehicleConfig vcfg_;

int control_freq = 10; // Hz
int warp = 1;

double datum_lat = goby::util::NaN<double>;
double datum_lon = goby::util::NaN<double>;
int duration = 0;

struct State
{
    double x, y, z, v, hdg;
};

State vehicle_{0, 0, 0, 0, 0};
State desired_{0, 0, 0, 0, 0};
int time_in_mission = 0;

std::unique_ptr<goby::util::UTMGeodesy> geodesy;

void parse_in(const std::string& in, std::map<std::string, std::string>* out);
bool started() { return !std::isnan(datum_lat) && !std::isnan(datum_lon); }

void update_desired(std::map<std::string, std::string>& parsed);
void update_start_params(std::map<std::string, std::string>& parsed);
void compute_state();

int main(int argc, char* argv[])
{
    if (argc < 2)
    {
        std::cout << "usage: basic_frontseat_modem_simulator [tcp listen port]" << std::endl;
        exit(1);
    }

    goby::util::TCPServer server(goby::util::as<unsigned>(argv[1]));
    server.start();

    while (!server.active()) sleep(1);

    int i = 0;
    while (server.active())
    {
        goby::util::protobuf::Datagram in;
        while (server.readline(&in))
        {
            // clear off \r\n and other whitespace at ends
            boost::trim(*in.mutable_data());

            std::map<std::string, std::string> parsed;
            try
            {
                parse_in(in.data(), &parsed);
                if (started() && parsed["KEY"] == "CMD")
                {
                    try
                    {
                        update_desired(parsed);
                        i = 0;
                        server.write("CMD,RESULT:OK\r\n");
                    }
                    catch (std::exception& e)
                    {
                        server.write("CMD,RESULT:ERROR\r\n");
                    }
                }
                else if (parsed["KEY"] == "START")
                {
                    std::cout << "Initialized using: " << in.data() << std::endl;
                    update_start_params(parsed);
                    server.write("CTRL,STATE:PAYLOAD\r\n");
                }
                else
                {
                    std::cerr << "Unknown key from payload: " << in.data() << std::endl;
                }
            }
            catch (std::exception& e)
            {
                std::cerr << "Invalid line from payload: " << in.data() << std::endl;
                std::cerr << "Why: " << e.what() << std::endl;
            }
        }

        time_in_mission++;
        if (started() && time_in_mission / control_freq > duration)
        {
            datum_lat = goby::util::NaN<double>;
            datum_lon = goby::util::NaN<double>;
            server.write("CTRL,STATE:IDLE\r\n");
        }

        if (started())
        {
            compute_state();

            auto ll = geodesy->convert(
                {vehicle_.x * boost::units::si::meters, vehicle_.y * boost::units::si::meters});
            std::stringstream nav_ss;
            nav_ss << "NAV,"
                   << "LAT:" << std::setprecision(10) << ll.lat.value() << ","
                   << "LON:" << std::setprecision(10) << ll.lon.value() << ","
                   << "DEPTH:" << -vehicle_.z << ","
                   << "HEADING:" << vehicle_.hdg << ","
                   << "SPEED:" << vehicle_.v << "\r\n";
            server.write(nav_ss.str());
        }
        usleep(1000000 / (control_freq * warp));
    }

    std::cerr << "server failed..." << std::endl;
    exit(1);
}

void parse_in(const std::string& in, std::map<std::string, std::string>* out)
{
    std::vector<std::string> comma_split;
    boost::split(comma_split, in, boost::is_any_of(","));
    out->insert(std::make_pair("KEY", comma_split.at(0)));
    for (int i = 1, n = comma_split.size(); i < n; ++i)
    {
        std::vector<std::string> colon_split;
        boost::split(colon_split, comma_split[i], boost::is_any_of(":"));
        out->insert(std::make_pair(colon_split.at(0), colon_split.at(1)));
    }
}

void update_desired(std::map<std::string, std::string>& parsed)
{
    if (!parsed.count("HEADING"))
        throw std::runtime_error("Invalid CMD: missing HEADING field");
    if (!parsed.count("SPEED"))
        throw std::runtime_error("Invalid CMD: missing SPEED field");
    if (!parsed.count("DEPTH"))
        throw std::runtime_error("Invalid CMD: missing DEPTH field");

    desired_.z = -goby::util::as<double>(parsed["DEPTH"]);
    desired_.v = goby::util::as<double>(parsed["SPEED"]);
    desired_.hdg = goby::util::as<double>(parsed["HEADING"]);
}

void update_start_params(std::map<std::string, std::string>& parsed)
{
    duration = 0;
    vcfg_ = VehicleConfig();
    control_freq = 10;
    warp = 1;

    if (!parsed.count("LAT"))
        throw std::runtime_error("Invalid START: missing LAT field");
    if (!parsed.count("LON"))
        throw std::runtime_error("Invalid START: missing LON field");
    datum_lat = goby::util::as<double>(parsed["LAT"]);
    datum_lon = goby::util::as<double>(parsed["LON"]);

    if (parsed.count("DURATION"))
        duration = goby::util::as<int>(parsed["DURATION"]);

    if (duration == 0)
        duration = std::numeric_limits<decltype(duration)>::max();

    if (parsed.count("FREQ"))
        control_freq = goby::util::as<int>(parsed["FREQ"]);
    if (parsed.count("ACCEL"))
        vcfg_.a = goby::util::as<double>(parsed["ACCEL"]);
    if (parsed.count("HDG_RATE"))
        vcfg_.hdg_rate = goby::util::as<double>(parsed["HDG_RATE"]);
    if (parsed.count("Z_RATE"))
        vcfg_.z_rate = goby::util::as<double>(parsed["Z_RATE"]);
    if (parsed.count("WARP"))
        warp = goby::util::as<int>(parsed["WARP"]);

    geodesy.reset(new goby::util::UTMGeodesy(
        {datum_lat * boost::units::degree::degrees, datum_lon * boost::units::degree::degrees}));

    time_in_mission = 0;
}

void compute_state()
{
    double dt = 1.0 / control_freq;

    if (std::abs(vehicle_.z - desired_.z) > vcfg_.z_rate * dt)
        vehicle_.z += (vehicle_.z < desired_.z) ? vcfg_.z_rate * dt : -vcfg_.z_rate * dt;
    else
        vehicle_.z = desired_.z;

    if ((vehicle_.hdg - desired_.hdg) >= 180)
        desired_.hdg += 360;
    if ((vehicle_.hdg - desired_.hdg) < -180)
        desired_.hdg -= 360;

    if (std::abs(vehicle_.hdg - desired_.hdg) > vcfg_.hdg_rate * dt)
        vehicle_.hdg += (vehicle_.hdg < desired_.hdg) ? vcfg_.hdg_rate * dt : -vcfg_.hdg_rate * dt;
    else
        vehicle_.hdg = desired_.hdg;

    if (std::abs(vehicle_.v - desired_.v) > vcfg_.a * dt)
        vehicle_.v += (vehicle_.v < desired_.v) ? vcfg_.a * dt : -vcfg_.a * dt;
    else
        vehicle_.v = desired_.v;

    double theta = (90 - vehicle_.hdg) * goby::util::pi<double> / 180;
    vehicle_.x += vehicle_.v * std::cos(theta) * dt;
    vehicle_.y += vehicle_.v * std::sin(theta) * dt;
}
