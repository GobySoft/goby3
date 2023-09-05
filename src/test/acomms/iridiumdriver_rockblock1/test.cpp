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

std::shared_ptr<goby::acomms::ModemDriverBase> mobile_driver, shore_driver;

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

    mobile_driver.reset(new goby::acomms::IridiumDriver);
    shore_driver.reset(new goby::acomms::IridiumShoreDriver);

    goby::acomms::protobuf::DriverConfig mobile_cfg, shore_cfg;

    mobile_cfg.set_modem_id(2);
    mobile_cfg.set_driver_type(goby::acomms::protobuf::DRIVER_IRIDIUM);
    mobile_cfg.set_connection_type(goby::acomms::protobuf::DriverConfig::CONNECTION_SERIAL);
    mobile_cfg.set_serial_port("/dev/ttyUSB0");
    mobile_cfg.set_serial_baud(19200);

    shore_cfg.set_modem_id(1);
    shore_cfg.set_driver_type(goby::acomms::protobuf::DRIVER_IRIDIUM_SHORE);
    goby::acomms::iridium::protobuf::ShoreConfig* shore_iridium_cfg =
        shore_cfg.MutableExtension(goby::acomms::iridium::protobuf::shore_config);
    goby::acomms::iridium::protobuf::ShoreConfig::ModemIDIMEIPair* mobile_id2imei =
        shore_iridium_cfg->add_modem_id_to_imei();
    mobile_id2imei->set_modem_id(mobile_cfg.modem_id());
    mobile_id2imei->set_imei("300434066863050");
    shore_iridium_cfg->set_sbd_type(goby::acomms::iridium::protobuf::ShoreConfig::SBD_ROCKBLOCK);
    shore_iridium_cfg->set_mo_sbd_server_port(52000);
    shore_iridium_cfg->set_mt_sbd_server_address("https://rockblock.rock7.com/rockblock/MT");
    shore_iridium_cfg->set_mt_sbd_server_port(80);
    shore_iridium_cfg->mutable_rockblock()->set_username("...");
    shore_iridium_cfg->mutable_rockblock()->set_password("...");

    std::vector<int> tests_to_run;
    tests_to_run.push_back(4);
    tests_to_run.push_back(5);

    mobile_driver->signal_modify_transmission.connect(
        [](goby::acomms::protobuf::ModemTransmission* msg)
        { msg->set_rate(goby::acomms::RATE_SBD); });

    goby::test::acomms::DriverTester tester(mobile_driver, shore_driver, mobile_cfg, shore_cfg,
                                            tests_to_run, goby::acomms::protobuf::DRIVER_IRIDIUM);
    return tester.run();
}
