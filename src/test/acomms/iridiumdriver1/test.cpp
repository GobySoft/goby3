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

// tests functionality of the Iridium Driver

#include "../../acomms/driver_tester/driver_tester.h"
#include "goby/acomms/modemdriver/iridium_driver.h"

std::shared_ptr<goby::acomms::ModemDriverBase> driver1, driver2;

using goby::acomms::iridium::protobuf::IridiumDriverConfig;

int main(int argc, char* argv[])
{
    goby::glog.add_stream(goby::util::logger::DEBUG3, &std::clog);
    std::ofstream fout;

    if (argc == 2)
    {
        fout.open(argv[1]);
        goby::glog.add_stream(goby::util::logger::DEBUG3, &fout);
    }

    goby::glog.set_name(argv[0]);

    driver1.reset(new goby::acomms::IridiumDriver);
    driver2.reset(new goby::acomms::IridiumDriver);

    goby::acomms::protobuf::DriverConfig glider_cfg, shore_cfg;

    // at-duck1
    glider_cfg.set_modem_id(1);
    glider_cfg.set_connection_type(goby::acomms::protobuf::DriverConfig::CONNECTION_TCP_AS_CLIENT);
    glider_cfg.set_tcp_server("127.0.0.1");
    glider_cfg.set_reconnect_interval(1);
    glider_cfg.set_tcp_port(4001);
    IridiumDriverConfig::Remote* shore = glider_cfg.MutableExtension(IridiumDriverConfig::remote);
    shore->set_iridium_number("6001");
    shore->set_modem_id(2);
    glider_cfg.AddExtension(IridiumDriverConfig::config, "+GSN");

    // at-duck2
    shore_cfg.set_modem_id(2);
    shore_cfg.set_connection_type(goby::acomms::protobuf::DriverConfig::CONNECTION_TCP_AS_CLIENT);
    shore_cfg.set_tcp_server("127.0.0.1");
    shore_cfg.set_reconnect_interval(1);
    shore_cfg.set_tcp_port(4002);
    IridiumDriverConfig::Remote* gumstix = shore_cfg.MutableExtension(IridiumDriverConfig::remote);
    gumstix->set_iridium_number("6001");
    gumstix->set_modem_id(2);
    //    shore_cfg.AddExtension(IridiumDriverConfig::config, "+GSN");

    //    shore
    //     {
    //         shore_cfg.set_modem_id(2);
    //         shore_cfg.set_connection_type(goby::acomms::protobuf::DriverConfig::CONNECTION_TCP_AS_CLIENT);
    //         shore_cfg.set_tcp_server("rudics.whoi.edu");
    //         shore_cfg.set_tcp_port(54321);
    //         IridiumDriverConfig::Remote* remote =
    //             shore_cfg.MutableExtension(IridiumDriverConfig::remote);
    //         remote->set_modem_id(1);
    //         remote->set_iridium_number("i881693783740");
    //         shore_cfg.AddExtension(IridiumDriverConfig::config, "s29=8");
    //         shore_cfg.AddExtension(IridiumDriverConfig::config, "s57=9600");
    //     }

    // //    gumstix
    //     {
    //         glider_cfg.set_modem_id(1);
    //         glider_cfg.set_connection_type(goby::acomms::protobuf::DriverConfig::CONNECTION_SERIAL);
    //         glider_cfg.set_serial_port("/dev/ttyUSB0");
    //         glider_cfg.set_serial_baud(9600);
    //         IridiumDriverConfig::Remote* remote =
    //             glider_cfg.MutableExtension(IridiumDriverConfig::remote);
    //         remote->set_iridium_number("i00881600005141");
    //         remote->set_modem_id(2);
    //         glider_cfg.AddExtension(IridiumDriverConfig::config, "+IPR=5,0");
    //         glider_cfg.AddExtension(IridiumDriverConfig::config, "+CGSN");
    //         glider_cfg.AddExtension(IridiumDriverConfig::config, "+CGMM");
    //         glider_cfg.AddExtension(IridiumDriverConfig::config, "+CGMI");
    //         glider_cfg.AddExtension(IridiumDriverConfig::config, "Q0"); // quiet mode off
    //         glider_cfg.AddExtension(IridiumDriverConfig::config, "&D2"); // DTR low hangs up
    //         glider_cfg.AddExtension(IridiumDriverConfig::config, "&C1"); // DCD indicates state of connection
    //     }

    std::vector<int> tests_to_run;
    tests_to_run.push_back(4);
    tests_to_run.push_back(5);

    goby::test::acomms::DriverTester tester(driver1, driver2, glider_cfg, shore_cfg, tests_to_run,
                                            goby::acomms::protobuf::DRIVER_IRIDIUM);
    return tester.run();
}
