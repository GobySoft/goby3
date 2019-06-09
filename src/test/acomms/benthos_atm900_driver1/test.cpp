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

// tests functionality of the Benthos ATM 900 Driver

#include "../../acomms/driver_tester/driver_tester.h"
#include "goby/acomms/modemdriver/benthos_atm900_driver.h"

std::shared_ptr<goby::acomms::ModemDriverBase> driver1, driver2;

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

    driver1.reset(new goby::acomms::BenthosATM900Driver);
    driver2.reset(new goby::acomms::BenthosATM900Driver);

    goby::acomms::protobuf::DriverConfig modem1_cfg, modem2_cfg;

    modem1_cfg.set_modem_id(1);
    modem2_cfg.set_modem_id(2);

    modem1_cfg.set_serial_port("/dev/ttyUSB0");
    modem2_cfg.set_serial_port("/dev/ttyUSB1");

    // add some simulated delay
    modem1_cfg.AddExtension(benthos::protobuf::BenthosATM900DriverConfig::config, "@SimAcDly=1000");
    modem2_cfg.AddExtension(benthos::protobuf::BenthosATM900DriverConfig::config, "@SimAcDly=1000");

    // turn down TxPower to minimum
    modem1_cfg.AddExtension(benthos::protobuf::BenthosATM900DriverConfig::config, "@TxPower=1");
    modem2_cfg.AddExtension(benthos::protobuf::BenthosATM900DriverConfig::config, "@TxPower=1");

    // go to lowpower quickly to test this
    modem1_cfg.AddExtension(benthos::protobuf::BenthosATM900DriverConfig::config,
                            "@IdleTimer=00:00:05");
    modem2_cfg.AddExtension(benthos::protobuf::BenthosATM900DriverConfig::config,
                            "@IdleTimer=00:00:05");

    std::vector<int> tests_to_run;
    tests_to_run.push_back(0);
    tests_to_run.push_back(4);
    tests_to_run.push_back(5);

    DriverTester tester(driver1, driver2, modem1_cfg, modem2_cfg, tests_to_run,
                        goby::acomms::protobuf::DRIVER_BENTHOS_ATM900);
    return tester.run();
}
