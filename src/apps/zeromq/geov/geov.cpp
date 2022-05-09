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

#include <arpa/inet.h>
#include <mysql/mysql.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include "goby/middleware/marshalling/protobuf.h"

#include "goby/middleware/frontseat/groups.h"
#include "goby/middleware/protobuf/frontseat_data.pb.h"
#include "goby/util/as.h"
#include "goby/zeromq/application/single_thread.h"
#include "goby/zeromq/protobuf/geov_config.pb.h"

using goby::glog;
namespace si = boost::units::si;
using ApplicationBase =
    goby::zeromq::SingleThreadApplication<goby::apps::zeromq::protobuf::GEOVInterfaceConfig>;

namespace goby
{
namespace apps
{
namespace zeromq
{
const std::string GE_CLIENT_ID = "2";

struct VehicleKey
{
    std::string name;
    std::string type;
};

bool operator<(const VehicleKey& k1, const VehicleKey& k2)
{
    return k1.name == k2.name ? k1.type < k2.type : k1.name < k2.name;
}

struct VehicleData
{
    VehicleKey key;
    std::string id;
    goby::time::SystemClock::time_point last_publish_t;
};

class GEOVInterface : public ApplicationBase
{
  public:
    GEOVInterface();

  private:
    void init_mysql();
    void handle_status(const goby::middleware::frontseat::protobuf::NodeStatus& frontseat_nav);

    std::string replace_vehicle_entry(std::string vtype, std::string vname, std::string newvid,
                                      std::string vloa, std::string vbeam);

    std::string escape(const std::string& query);
    void print_error(MYSQL* conn, const char* message);

  private:
    std::map<VehicleKey, VehicleData> known_vehicles_;
    int simulation_user_{0};
    goby::time::SystemClock::duration pos_dt_;

    MYSQL* core_connection_{nullptr};
};
} // namespace zeromq
} // namespace apps
} // namespace goby

int main(int argc, char* argv[])
{
    return goby::run<goby::apps::zeromq::GEOVInterface>(argc, argv);
}

goby::apps::zeromq::GEOVInterface::GEOVInterface()
    : pos_dt_(goby::time::convert_duration<decltype(pos_dt_)>(
          cfg().position_report_interval_with_units()))
{
    interprocess().subscribe<goby::middleware::frontseat::groups::node_status>(
        [this](const goby::middleware::frontseat::protobuf::NodeStatus& frontseat_nav) {
            handle_status(frontseat_nav);
        });
    init_mysql();
}

void goby::apps::zeromq::GEOVInterface::init_mysql()
{
    // initialize connection handler
    core_connection_ = mysql_init(nullptr);

    bool reconnect = true;
    mysql_options(core_connection_, MYSQL_OPT_RECONNECT, &reconnect);

    if (core_connection_ == nullptr)
    {
        glog.is_die() && glog << "mysql_init() failed (probably out of memory)" << std::endl;
    }
    /// connect to mysql server
    if (mysql_real_connect(core_connection_, cfg().mysql_host().c_str(), cfg().mysql_user().c_str(),
                           cfg().mysql_password().c_str(), cfg().mysql_core_db_name().c_str(),
                           cfg().mysql_port(), "/var/run/mysqld/mysqld.sock", 0) == nullptr)
    {
        mysql_close(core_connection_);
        glog.is_die() && glog << "core mysql_real_connect() failed\n"
                              << cfg().DebugString() << std::endl;
    }

    // select core db
    if (mysql_select_db(core_connection_, cfg().mysql_core_db_name().c_str()) != 0)
    {
        glog.is_die() && glog << "could not select core database" << std::endl;
    }
    glog.is_verbose() && glog << "successfully initialized and opened core mysql connection"
                              << std::endl;

    // default to real user
    simulation_user_ = 0;

    if (cfg().simulation())
    {
        MYSQL_RES* res_set(nullptr);

        std::string query = "SELECT USER()";
        std::string ip, host;

        if (mysql_query(core_connection_, query.c_str()) != 0)
        {
            print_error(core_connection_, "mysql_query() failed");
        }
        else
        {
            res_set = mysql_store_result(core_connection_);
            MYSQL_ROW row;
            row = mysql_fetch_row(res_set);
            std::string user = row[0];
            std::vector<std::string> user_parts;
            boost::split(user_parts, user, boost::is_any_of("@"));
            host = user_parts[1];

            struct hostent* hp = gethostbyname(host.c_str());

            if (hp == nullptr)
            {
                glog.is_die() && glog << "gethostbyname() failed" << std::endl;
            }
            else
            {
                ip = inet_ntoa(*(struct in_addr*)(hp->h_addr_list[0]));
            }
        }
        query = "SELECT connected_userid, user_name FROM core_connected JOIN core_user ON "
                "user_id=connected_userid WHERE connected_ip = '" +
                ip + "' AND connected_client = " + GE_CLIENT_ID;

        glog.is_debug1() && glog << group("select") << query << std::endl;

        if (mysql_query(core_connection_, query.c_str()) != 0)
        {
            print_error(core_connection_, "mysql_query() failed");
        }
        else
        {
            res_set = mysql_store_result(core_connection_);

            if (res_set == nullptr)
                print_error(core_connection_, "mysql_store_result() failed");
            else
            {
                if (mysql_num_rows(res_set) == 0)
                {
                    goby::glog.is_die() && goby::glog
                                               << "no profile bound to this IP address (" << ip
                                               << ") for simulation use. you must bind such a "
                                                  "profile first using the geov profile manager."
                                               << std::endl;
                }
                else
                {
                    //get user id
                    MYSQL_ROW row;
                    row = mysql_fetch_row(res_set);
                    simulation_user_ = std::atoi(row[0]);
                    std::string sim_name = row[1];
                    glog.is_verbose() && glog << "inputting simulation data for user " << sim_name
                                              << "(" << simulation_user_ << ") at IP: " << ip
                                              << std::endl;
                }
            }
        }
    }
}

void goby::apps::zeromq::GEOVInterface::handle_status(
    const goby::middleware::frontseat::protobuf::NodeStatus& frontseat_nav)
{
    // determine vehicle id or add new vehicle
    std::string vname = frontseat_nav.name();
    std::string vtype =
        goby::middleware::frontseat::protobuf::VehicleType_Name(frontseat_nav.type());

    VehicleKey vkey{vname, vtype};
    auto vit = known_vehicles_.find(vkey);
    std::string vid;
    if (vit == known_vehicles_.end())
    {
        MYSQL_RES* res_set;

        std::string query_veh = "SELECT vehicle_id FROM core_vehicle WHERE ";
        query_veh += "(lower(vehicle_name) = '" + escape(boost::to_lower_copy(vname));
        query_veh += "' AND lower(vehicle_type) = '" + escape(boost::to_lower_copy(vtype));
        query_veh += "')";

        glog.is_debug1() && glog << group("select") << query_veh << std::endl;

        if (mysql_query(core_connection_, query_veh.c_str()) != 0)
        {
            print_error(core_connection_, "mysql_query() failed");
        }
        else
        {
            res_set = mysql_store_result(core_connection_);

            if (!res_set)
            {
                print_error(core_connection_, "mysql_store_result() failed");
            }
            else
            {
                // if you have no id already for this, or the vname and vtype aren't both set
                // you need to add an entry for this vehicle
                if (mysql_num_rows(res_set) == 0)
                {
                    MYSQL_RES* res_set2;

                    std::string newvid;

                    std::string query_find_next_id = "SELECT MAX(vehicle_id)+1 FROM core_vehicle "
                                                     "WHERE vehicle_id < 100000000";
                    if (mysql_query(core_connection_, query_find_next_id.c_str()) != 0)
                    {
                        print_error(core_connection_, "mysql_query() failed");
                    }
                    else
                    {
                        res_set2 = mysql_store_result(core_connection_);
                        MYSQL_ROW row = mysql_fetch_row(res_set2);
                        newvid = row[0];
                    }

                    vid = replace_vehicle_entry(vtype, vname, newvid, "", "");
                }
                else if (!(mysql_num_rows(res_set) == 0))
                {
                    //get veh id
                    MYSQL_ROW row;
                    row = mysql_fetch_row(res_set);
                    vid = row[0];
                }

                glog.is_debug1() && glog << "vehicle id is " << vid << "." << std::endl;

                mysql_free_result(res_set);
            }
        }

        VehicleData newvdata{vkey, vid};
        auto vit_success_pair = known_vehicles_.insert(std::make_pair(vkey, newvdata));
        vit = vit_success_pair.first;
    }
    else
    {
        vid = vit->second.id;
    }

    // check the blackout time on this vehicle.
    auto message_time = goby::time::convert<goby::time::SystemClock::time_point>(
             frontseat_nav.time_with_units()),
         &last_time = vit->second.last_publish_t;
    if (!(message_time > last_time + pos_dt_))
        return;

    last_time = message_time;

    std::string nlat = goby::util::as<std::string>(frontseat_nav.global_fix().lat());
    std::string nlong = goby::util::as<std::string>(frontseat_nav.global_fix().lon());

    // unwarp time for use in GEOV
    double utc_time = goby::time::convert<goby::time::SITime>(
                          goby::time::SystemClock::unwarp(
                              goby::time::convert<goby::time::SystemClock::time_point>(
                                  frontseat_nav.time_with_units())))
                          .value();

    std::string nav_hdg_val = goby::util::as<std::string>(
        frontseat_nav.pose()
            .heading_with_units<boost::units::quantity<boost::units::degree::plane_angle>>()
            .value());
    std::string nav_spd_val = goby::util::as<std::string>(
        frontseat_nav.speed()
            .over_ground_with_units<boost::units::quantity<boost::units::si::velocity>>()
            .value());
    std::string nav_dep_val = goby::util::as<std::string>(
        frontseat_nav.global_fix()
            .depth_with_units<boost::units::quantity<boost::units::si::length>>()
            .value());

    std::string table = "core_data";
    std::string query_insert = "INSERT INTO " + table + " (data_vehicleid, ";
    query_insert +=
        "data_userid, data_time, data_lat, data_long, data_heading, data_speed, data_depth ) ";
    query_insert += "VALUES ('" + escape(vid);
    query_insert += "', '" + goby::util::as<std::string>(simulation_user_);
    query_insert += "', '" + escape(goby::util::as<std::string>(utc_time));
    query_insert += "', '" + escape(nlat);
    query_insert += "', '" + escape(nlong);
    query_insert += "', '" + escape(nav_hdg_val);
    query_insert += "', '" + escape(nav_spd_val);
    query_insert += "', '" + escape(nav_dep_val);
    query_insert += "')";

    glog.is_debug1() && glog << group("insert") << query_insert << std::endl;

    if (mysql_query(core_connection_, query_insert.c_str()) != 0)
    {
        print_error(core_connection_, "insert failed");
    }
}

std::string goby::apps::zeromq::GEOVInterface::replace_vehicle_entry(
    std::string vtype, std::string vname, std::string newvid, std::string vloa, std::string vbeam)
{
    //add new id
    std::string query_veh_insert =
        "REPLACE INTO core_vehicle (vehicle_type, vehicle_name, vehicle_id";
    if (vloa != "")
        query_veh_insert += ",vehicle_loa";
    if (vbeam != "")
        query_veh_insert += ",vehicle_beam";

    query_veh_insert += ") ";
    query_veh_insert += "VALUES ('" + escape(boost::to_lower_copy(vtype));
    query_veh_insert += "', '" + escape(boost::to_lower_copy(vname));
    query_veh_insert += "', '" + escape(newvid);

    if (vloa != "")
        query_veh_insert += "', '" + escape(vloa);
    if (vbeam != "")
        query_veh_insert += "', '" + escape(vbeam);

    query_veh_insert += "')";

    glog.is_debug1() && glog << group("insert") << query_veh_insert << std::endl;

    if (mysql_query(core_connection_, query_veh_insert.c_str()) != 0)
    {
        print_error(core_connection_, "insert failed");
        return "0";
    }
    else
    {
        return goby::util::as<std::string>(mysql_insert_id(core_connection_));
    }
}

std::string goby::apps::zeromq::GEOVInterface::escape(const std::string& s)
{
    unsigned long l = s.length();

    char c[l * 2 + 1];
    mysql_real_escape_string(core_connection_, c, s.c_str(), l);
    return c;
}

void goby::apps::zeromq::GEOVInterface::print_error(MYSQL* conn, const char* message)
{
    glog.is_warn() && glog << message << std::endl;

    if (conn)
    {
        glog.is_warn() && glog << "Error" << mysql_errno(conn) << "(" << mysql_sqlstate(conn)
                               << "): " << mysql_error(conn) << std::endl;
    }
}
