// Copyright 2009-2017 Toby Schneider (http://gobysoft.org/index.wt/people/toby)
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

#include "goby/pb/pb_modem_driver.h"
#include "goby/common/logger.h"
#include "goby/util/binary.h"
#include "goby/acomms/connect.h"
#include "goby/acomms/acomms_helpers.h"
#include "../../acomms/driver_tester/driver_tester.h"

using namespace goby::common::logger;
using namespace goby::acomms;
using goby::util::as;
using goby::common::goby_time;
using namespace boost::posix_time;

int main(int argc, char* argv[])
{
    boost::shared_ptr<goby::pb::PBDriver> driver1, driver2;
    goby::glog.add_stream(goby::common::logger::DEBUG3, &std::clog);
    std::ofstream fout;

    if(argc == 2)
    {
        fout.open(argv[1]);
        goby::glog.add_stream(goby::common::logger::DEBUG3, &fout);        
    }
    
    goby::glog.set_name(argv[0]);    

    goby::glog.add_group("test", goby::common::Colors::green);
    goby::glog.add_group("driver1", goby::common::Colors::green);
    goby::glog.add_group("driver2", goby::common::Colors::yellow);

    goby::common::ZeroMQService zeromq_service1, zeromq_service2;
    
    driver1.reset(new goby::pb::PBDriver(&zeromq_service1));
    driver2.reset(new goby::pb::PBDriver(&zeromq_service2));
    
    goby::acomms::protobuf::DriverConfig cfg1, cfg2;
        
    cfg1.set_modem_id(1);

    goby::common::protobuf::ZeroMQServiceConfig::Socket* socket1 =
        cfg1.MutableExtension(PBDriverConfig::request_socket);
        
    socket1->set_socket_type(goby::common::protobuf::ZeroMQServiceConfig::Socket::REQUEST);
    socket1->set_transport(goby::common::protobuf::ZeroMQServiceConfig::Socket::TCP);
    socket1->set_connect_or_bind(goby::common::protobuf::ZeroMQServiceConfig::Socket::CONNECT);
    socket1->set_ethernet_address("127.0.0.1");
    socket1->set_ethernet_port(54321);

    cfg1.SetExtension(PBDriverConfig::query_interval_seconds, 2);
    cfg1.AddExtension(PBDriverConfig::rate_to_frames, 1);
    cfg1.AddExtension(PBDriverConfig::rate_to_frames, 3);
    cfg1.AddExtension(PBDriverConfig::rate_to_frames, 3);
    cfg1.AddExtension(PBDriverConfig::rate_to_bytes, 32);
    cfg1.AddExtension(PBDriverConfig::rate_to_bytes, 64);
    cfg1.AddExtension(PBDriverConfig::rate_to_bytes, 64);
        
    cfg2.set_modem_id(2);
    cfg2.MutableExtension(PBDriverConfig::request_socket)->CopyFrom(*socket1);
        
    std::vector<int> tests_to_run;
    tests_to_run.push_back(4);
    tests_to_run.push_back(5);
    
    DriverTester tester(driver1, driver2, cfg1, cfg2, tests_to_run, goby::acomms::protobuf::DRIVER_PB_STORE_SERVER);
    return tester.run();    
}

