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

// tests functionality of the Goby PBDriver, using goby_store_server

#include "../driver_tester/driver_tester.h"
#include "goby/acomms/connect.h"
#include "goby/acomms/modemdriver/store_server_driver.h"
#include "goby/util/binary.h"
#include "goby/util/debug_logger.h"

using namespace goby::util::logger;
using namespace goby::acomms;
using goby::util::as;
using namespace boost::posix_time;

int main(int argc, char* argv[])
{
    std::shared_ptr<goby::acomms::StoreServerDriver> driver1, driver2;
    goby::glog.add_stream(goby::util::logger::DEBUG3, &std::clog);
    std::ofstream fout;

    if (argc == 2)
    {
        fout.open(argv[1]);
        goby::glog.add_stream(goby::util::logger::DEBUG3, &fout);
    }

    goby::glog.set_name(argv[0]);

    goby::glog.add_group("test", goby::util::Colors::green);
    goby::glog.add_group("driver1", goby::util::Colors::green);
    goby::glog.add_group("driver2", goby::util::Colors::yellow);

    driver1.reset(new goby::acomms::StoreServerDriver);
    driver2.reset(new goby::acomms::StoreServerDriver);

    goby::acomms::protobuf::DriverConfig cfg1, cfg2;

    cfg1.set_modem_id(1);
    constexpr const char* store_server_default_ip = "127.0.0.1";
    cfg1.set_connection_type(goby::acomms::protobuf::DriverConfig::CONNECTION_TCP_AS_CLIENT);
    cfg2.set_connection_type(goby::acomms::protobuf::DriverConfig::CONNECTION_TCP_AS_CLIENT);
    cfg1.set_line_delimiter("\r");
    cfg2.set_line_delimiter("\r");
    cfg1.set_tcp_server(store_server_default_ip);
    cfg2.set_tcp_server(store_server_default_ip);

    auto& store_server_cfg1 = *cfg1.MutableExtension(goby::acomms::store_server::protobuf::config);

    store_server_cfg1.set_query_interval_seconds(2);
    store_server_cfg1.add_rate_to_frames(1);
    store_server_cfg1.add_rate_to_frames(3);
    store_server_cfg1.add_rate_to_frames(3);

    store_server_cfg1.add_rate_to_bytes(32);
    store_server_cfg1.add_rate_to_bytes(64);
    store_server_cfg1.add_rate_to_bytes(64);

    cfg2.set_modem_id(2);

    std::vector<int> tests_to_run;
    tests_to_run.push_back(4);
    tests_to_run.push_back(5);

    goby::test::acomms::DriverTester tester(driver1, driver2, cfg1, cfg2, tests_to_run,
                                            goby::acomms::protobuf::DRIVER_STORE_SERVER);
    return tester.run();
}
