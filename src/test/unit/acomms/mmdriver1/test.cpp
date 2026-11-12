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

// tests functionality of the MMDriver WHOI Micro-Modem driver

#include "../driver_tester/driver_tester.h"
#include "goby/acomms/modemdriver/mm_driver.h"

namespace micromodem = goby::acomms::micromodem;

std::shared_ptr<goby::acomms::ModemDriverBase> driver1, driver2;

int main(int argc, char* argv[])
{
    if (argc < 3)
    {
        std::cout
            << "usage: test_mmdriver1 /dev/ttyS0 /dev/ttyS1 [file to write] [mm version (1 or 2)]"
            << std::endl;
        exit(1);
    }

    goby::glog.add_stream(goby::util::logger::DEBUG3, &std::clog);
    std::ofstream fout;
    if (argc >= 4)
    {
        fout.open(argv[3]);
        goby::glog.add_stream(goby::util::logger::DEBUG3, &fout);
    }
    int mm_version = 1;
    if (argc == 5)
    {
        mm_version = goby::util::as<int>(argv[4]);
    }

    goby::glog.set_name(argv[0]);

    goby::acomms::protobuf::DriverConfig cfg1, cfg2;
    auto* mm_cfg1 = cfg1.MutableExtension(micromodem::protobuf::config);
    auto* mm_cfg2 = cfg2.MutableExtension(micromodem::protobuf::config);

    cfg1.set_serial_port(argv[1]);
    cfg1.set_modem_id(1);
    // 0111
    mm_cfg1->mutable_remus_lbl()->set_enable_beacons(7);

    mm_cfg1->set_reset_nvram(true);
    mm_cfg2->set_reset_nvram(true);

    // so we can play with the emulator box BNC cables and expect bad CRC'S (otherwise crosstalk is enough to receive everything ok!)
    mm_cfg1->add_nvram_cfg("AGC,0");
    mm_cfg2->add_nvram_cfg("AGC,0");
    mm_cfg1->add_nvram_cfg("AGN,0");
    mm_cfg2->add_nvram_cfg("AGN,0");

    cfg2.set_serial_port(argv[2]);
    cfg2.set_modem_id(2);

    std::vector<int> tests_to_run;
    tests_to_run.push_back(0);
    if (mm_version == 1)
    {
        // ranging, mini-data not yet supported by MM2?
        tests_to_run.push_back(1);
        tests_to_run.push_back(2);
        tests_to_run.push_back(3);
    }

    tests_to_run.push_back(4);
    tests_to_run.push_back(5);

    // FDP only supported in MM2
    if (mm_version == 2)
    {
        tests_to_run.push_back(6);
        mm_cfg1->add_nvram_cfg("psk.packet.mod_hdr_version,0");
        mm_cfg2->add_nvram_cfg("psk.packet.mod_hdr_version,0");
    }

    driver1.reset(new goby::acomms::MMDriver);
    driver2.reset(new goby::acomms::MMDriver);

    goby::test::acomms::DriverTester tester(driver1, driver2, cfg1, cfg2, tests_to_run,
                                            goby::acomms::protobuf::DRIVER_WHOI_MICROMODEM);
    return tester.run();
}
