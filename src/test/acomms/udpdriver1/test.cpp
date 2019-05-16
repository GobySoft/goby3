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

// tests functionality of the UDPDriver

#include "../driver_tester/driver_tester.h"
#include "goby/acomms/modemdriver/udp_driver.h"
#include <cstdlib>

boost::asio::io_service io1, io2;
std::shared_ptr<goby::acomms::ModemDriverBase> driver1, driver2;

void handle_raw_incoming(int driver, const goby::acomms::protobuf::ModemRaw& raw)
{
    std::cout << "Raw in (" << driver << "): " << raw.ShortDebugString() << std::endl;
}

void handle_raw_outgoing(int driver, const goby::acomms::protobuf::ModemRaw& raw)
{
    std::cout << "Raw out (" << driver << "): " << raw.ShortDebugString() << std::endl;
}

int main(int argc, char* argv[])
{
    goby::glog.add_stream(goby::common::logger::DEBUG3, &std::clog);
    std::ofstream fout;

    if (argc == 2)
    {
        fout.open(argv[1]);
        goby::glog.add_stream(goby::common::logger::DEBUG3, &fout);
    }

    goby::glog.set_name(argv[0]);

    driver1.reset(new goby::acomms::UDPDriver(&io1));
    driver2.reset(new goby::acomms::UDPDriver(&io2));

    goby::acomms::connect(&driver1->signal_raw_incoming, boost::bind(&handle_raw_incoming, 1, _1));
    goby::acomms::connect(&driver2->signal_raw_incoming, boost::bind(&handle_raw_incoming, 2, _1));
    goby::acomms::connect(&driver1->signal_raw_outgoing, boost::bind(&handle_raw_outgoing, 1, _1));
    goby::acomms::connect(&driver2->signal_raw_outgoing, boost::bind(&handle_raw_outgoing, 2, _1));

    goby::acomms::protobuf::DriverConfig cfg1, cfg2;

    cfg1.set_modem_id(1);

    srand(time(NULL));
    int port1 = rand() % 1000 + 50000;
    int port2 = port1 + 1;

    //gumstix
    UDPDriverConfig::EndPoint* local_endpoint1 = cfg1.MutableExtension(UDPDriverConfig::local);
    local_endpoint1->set_port(port1);

    cfg2.set_modem_id(2);

    // shore
    UDPDriverConfig::EndPoint* local_endpoint2 = cfg2.MutableExtension(UDPDriverConfig::local);
    local_endpoint2->set_port(port2);

    UDPDriverConfig::EndPoint* remote_endpoint1 = cfg1.MutableExtension(UDPDriverConfig::remote);

    remote_endpoint1->set_ip("localhost");
    remote_endpoint1->set_port(port2);

    UDPDriverConfig::EndPoint* remote_endpoint2 = cfg2.MutableExtension(UDPDriverConfig::remote);

    remote_endpoint2->set_ip("127.0.0.1");
    remote_endpoint2->set_port(port1);

    std::vector<int> tests_to_run;
    tests_to_run.push_back(4);
    tests_to_run.push_back(5);

    DriverTester tester(driver1, driver2, cfg1, cfg2, tests_to_run,
                        goby::acomms::protobuf::DRIVER_UDP);
    return tester.run();
}
