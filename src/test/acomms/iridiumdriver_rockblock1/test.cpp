// Copyright 2011-2020:
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

// tests functionality of the Iridium Driver

#include "../../acomms/driver_tester/driver_tester.h"
#include "goby/acomms/modemdriver/iridium_driver.h"
#include "goby/acomms/modemdriver/iridium_shore_driver.h"
#include "goby/time/steady_clock.h"

std::shared_ptr<goby::acomms::ModemDriverBase> mobile_driver, shore_driver;

const bool using_simulator{true};

int main(int argc, char* argv[])
{
    goby::glog.add_stream(goby::util::logger::DEBUG3, &std::clog);
    std::ofstream fout;

    std::string username = "dummyuser";
    std::string password = "dummypassword";

    if (!using_simulator)
    {
        if (argc != 3)
        {
            std::cerr
                << "Usage: goby_test_iridiumdriver_rockblock1 rockblock_username rockblock_password"
                << std::endl;
            exit(1);
        }
        username = argv[1];
        password = argv[2];
    }

    goby::glog.set_name(argv[0]);

    mobile_driver.reset(new goby::acomms::IridiumDriver);
    shore_driver.reset(new goby::acomms::IridiumShoreDriver);

    goby::acomms::protobuf::DriverConfig mobile_cfg, shore_cfg;

    mobile_cfg.set_modem_id(2);
    mobile_cfg.set_driver_type(goby::acomms::protobuf::DRIVER_IRIDIUM);
    mobile_cfg.set_connection_type(goby::acomms::protobuf::DriverConfig::CONNECTION_SERIAL);

    if (using_simulator)
        mobile_cfg.set_serial_port("/tmp/ttyrockblock");
    else
        mobile_cfg.set_serial_port("/dev/ttyUSB0");

    mobile_cfg.set_serial_baud(19200);
    goby::acomms::iridium::protobuf::Config* mobile_iridium_cfg =
        mobile_cfg.MutableExtension(goby::acomms::iridium::protobuf::config);
    mobile_iridium_cfg->add_config("+SBDMTA=1"); // SBDRING
    mobile_iridium_cfg->add_config("+SBDAREG=1");
    mobile_iridium_cfg->add_config("+CIER=1,1,1");

    shore_cfg.set_modem_id(1);
    shore_cfg.set_driver_type(goby::acomms::protobuf::DRIVER_IRIDIUM_SHORE);
    goby::acomms::iridium::protobuf::ShoreConfig* shore_iridium_cfg =
        shore_cfg.MutableExtension(goby::acomms::iridium::protobuf::shore_config);
    goby::acomms::iridium::protobuf::ShoreConfig::ModemIDIMEIPair* mobile_id2imei =
        shore_iridium_cfg->add_modem_id_to_imei();
    mobile_id2imei->set_modem_id(mobile_cfg.modem_id());
    mobile_id2imei->set_imei("300434066863050");
    shore_iridium_cfg->set_sbd_type(goby::acomms::iridium::protobuf::ShoreConfig::SBD_ROCKBLOCK);
    shore_iridium_cfg->set_mo_sbd_server_port(8080);
    if (using_simulator)
    {
        shore_iridium_cfg->mutable_rockblock()->set_server("http://127.0.0.1:8081");
        shore_iridium_cfg->mutable_rockblock()->set_skip_jwt_verification(true);
    }

    shore_iridium_cfg->mutable_rockblock()->set_username(username);
    shore_iridium_cfg->mutable_rockblock()->set_password(password);

    std::vector<int> tests_to_run;
    tests_to_run.push_back(4);
    tests_to_run.push_back(5);

    mobile_driver->signal_modify_transmission.connect(
        [](goby::acomms::protobuf::ModemTransmission* msg)
        { msg->set_rate(goby::acomms::RATE_SBD); });
    shore_driver->signal_modify_transmission.connect(
        [](goby::acomms::protobuf::ModemTransmission* msg)
        { msg->set_rate(goby::acomms::RATE_SBD); });

    if (!using_simulator)
    {
        // clear any messages out
        shore_iridium_cfg->mutable_rockblock()->set_server("invalid");
        shore_driver->startup(shore_cfg);
        std::cout << "Clearing any pending MO message" << std::endl;
        auto end = goby::time::SteadyClock::now() + std::chrono::seconds(60);
        while (goby::time::SteadyClock::now() < end)
        {
            shore_driver->do_work();
            usleep(10000);
        }
        shore_driver->shutdown();
        shore_iridium_cfg->mutable_rockblock()->clear_server();
    }

    goby::test::acomms::DriverTester tester(shore_driver, mobile_driver, shore_cfg, mobile_cfg,
                                            tests_to_run, goby::acomms::protobuf::DRIVER_IRIDIUM);
    return tester.run();
}
