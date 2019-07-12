// Copyright 2009-2018 Toby Schneider (http://gobysoft.org/index.wt/people/toby)
//                     GobySoft, LLC (2013-)
//                     Massachusetts Institute of Technology (2007-2014)
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
// 1. run abc_frontseat_simulator running on some port (as TCP server)
// > abc_modem_simulator 54321
// 2. run iFrontSeat connecting to that port

#include <boost/math/special_functions/fpclassify.hpp>
#include <limits>
#include <map>
#include <sstream>
#include <string>

#include "goby/moos/moos_geodesy.h"
#include "goby/util/as.h"
#include "goby/util/linebasedcomms.h"

double datum_lat = std::numeric_limits<double>::quiet_NaN();
double datum_lon = std::numeric_limits<double>::quiet_NaN();
int duration = 0;

double current_x = 0;
double current_y = 0;
double current_z = 0;
double current_v = 0;
double current_hdg = 0;

goby::moos::CMOOSGeodesy geodesy;

void parse_in(const std::string& in, std::map<std::string, std::string>* out);
bool started() { return !std::isnan(datum_lat) && !std::isnan(datum_lon); }

int main(int argc, char* argv[])
{
    if (argc < 2)
    {
        std::cout << "usage: abc_modem_simulator [tcp listen port]" << std::endl;
        exit(1);
    }

    goby::util::TCPServer server(goby::util::as<unsigned>(argv[1]));
    server.start();
    sleep(1);

    int i = 0;
    int time_in_mission = 0;
    while (server.active())
    {
        goby::util::protobuf::Datagram in;
        while (server.readline(&in))
        {
            // clear off \r\n and other whitespace at ends
            boost::trim(*in.mutable_data());

            std::cout << "Received: " << in.ShortDebugString() << std::endl;

            std::map<std::string, std::string> parsed;
            try
            {
                parse_in(in.data(), &parsed);
                if (started() && parsed["KEY"] == "CMD")
                {
                    try
                    {
                        if (!parsed.count("HEADING"))
                            throw std::runtime_error("Invalid CMD: missing HEADING field");
                        if (!parsed.count("SPEED"))
                            throw std::runtime_error("Invalid CMD: missing SPEED field");
                        if (!parsed.count("DEPTH"))
                            throw std::runtime_error("Invalid CMD: missing DEPTH field");

                        current_z = -goby::util::as<double>(parsed["DEPTH"]);
                        current_v = goby::util::as<double>(parsed["SPEED"]);
                        current_hdg = goby::util::as<double>(parsed["HEADING"]);
                        std::cout << "Updated z: " << current_z << " m, v: " << current_v
                                  << " m/s, heading: " << current_hdg << " deg" << std::endl;

                        server.write("CMD,RESULT:OK\r\n");
                    }
                    catch (std::exception& e)
                    {
                        server.write("CMD,RESULT:ERROR\r\n");
                    }
                }
                else if (parsed["KEY"] == "START")
                {
                    if (!parsed.count("LAT"))
                        throw std::runtime_error("Invalid START: missing LAT field");
                    if (!parsed.count("LON"))
                        throw std::runtime_error("Invalid START: missing LON field");
                    if (!parsed.count("DURATION"))
                        throw std::runtime_error("Invalid START: missing DURATION field");
                    datum_lat = goby::util::as<double>(parsed["LAT"]);
                    datum_lon = goby::util::as<double>(parsed["LON"]);
                    duration = goby::util::as<int>(parsed["DURATION"]);
                    geodesy.Initialise(datum_lat, datum_lon);

                    time_in_mission = 0;
                    server.write("CTRL,STATE:PAYLOAD\r\n");
                }
                else
                {
                    std::cout << "Unknown key from payload: " << in.data() << std::endl;
                }
            }
            catch (std::exception& e)
            {
                std::cout << "Invalid line from payload: " << in.data() << std::endl;
                std::cout << "Why: " << e.what() << std::endl;
            }
        }
        usleep(1000);
        ++i;
        if (i == 1000)
        {
            i = 0;
            time_in_mission++;
            if (started() && time_in_mission > duration)
            {
                datum_lat = std::numeric_limits<double>::quiet_NaN();
                datum_lon = std::numeric_limits<double>::quiet_NaN();
                server.write("CTRL,STATE:IDLE\r\n");
            }

            if (started())
            {
                double theta = (90 - current_hdg) * 3.14 / 180;
                current_x += current_v * std::cos(theta);
                current_y += current_v * std::sin(theta);

                std::cout << "new x: " << current_x << ", y: " << current_y << std::endl;

                double lat, lon;
                geodesy.UTM2LatLong(current_x, current_y, lat, lon);
                std::stringstream nav_ss;
                nav_ss << "NAV,"
                       << "LAT:" << std::setprecision(10) << lat << ","
                       << "LON:" << std::setprecision(10) << lon << ","
                       << "DEPTH:" << -current_z << ","
                       << "HEADING:" << current_hdg << ","
                       << "SPEED:" << current_v << "\r\n";
                server.write(nav_ss.str());
            }
        }
    }

    std::cout << "server failed..." << std::endl;
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
