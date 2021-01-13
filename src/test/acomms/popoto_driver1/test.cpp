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

// tests functionality of the PopotoDriver. Tested with:

// Using Popoto Virtual Ocean (pvo) Popoto Modem Version 2.6.3  223
// cd popoto-pvo/test/pvo
// ./pvo.py GobyTwoChan.cfg
// socat pty,link=/tmp/ttyvmodem0,raw,echo=0 exec:"python ../scripts/pshell \'connect localhost 17500\' \'startrx\'",pty
// socat pty,link=/tmp/ttyvmodem1,raw,echo=0 exec:"python ../scripts/pshell \'connect localhost 18500\' \'startrx\'",pty
// goby_test_popoto_driver1

// diff -u TwoChan.cfg GobyTwoChan.cfg
// --- TwoChan.cfg	2020-11-17 12:35:59.000000000 -0500
// +++ GobyTwoChan.cfg	2020-12-02 12:14:18.857584002 -0500
// @@ -22,7 +22,7 @@
//              "X":0,
//              "fgcolor":"white",
//              "txdBSpl":"180",
// -            "useShell":"TRUE"
// +            "useShell":"false"
//          },
//          {
//              "shell":"python ../scripts/pshell 'connect {} {}' 'startrx'",
// @@ -34,12 +34,12 @@
//              "bgcolor":"black",
//              "log":"FALSE",
//              "Y":0,
// -            "X":500,
// +            "X":100,
//              "fgcolor":"white",
//              "txdBSpl":"180",
// -            "useShell":"TRUE"
// +            "useShell":"false"
//          }

//      ]

// -}
// \ No newline at end of file
// +}

#include "../driver_tester/driver_tester.h"
#include "goby/acomms/modemdriver/popoto_driver.h"
#include <cstdlib>


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
    goby::glog.add_stream(goby::util::logger::DEBUG3, &std::clog);
    std::ofstream fout;

    if (argc == 2)
    {
        fout.open(argv[1]);
        goby::glog.add_stream(goby::util::logger::DEBUG3, &fout);
    }

    goby::glog.set_name(argv[0]);

    driver1.reset(new goby::acomms::PopotoDriver);
    driver2.reset(new goby::acomms::PopotoDriver);

    goby::acomms::connect(&driver1->signal_raw_incoming,
                          std::bind(&handle_raw_incoming, 1, std::placeholders::_1));
    goby::acomms::connect(&driver2->signal_raw_incoming,
                          std::bind(&handle_raw_incoming, 2, std::placeholders::_1));
    goby::acomms::connect(&driver1->signal_raw_outgoing,
                          std::bind(&handle_raw_outgoing, 1, std::placeholders::_1));
    goby::acomms::connect(&driver2->signal_raw_outgoing,
                          std::bind(&handle_raw_outgoing, 2, std::placeholders::_1));

    goby::acomms::protobuf::DriverConfig cfg1, cfg2;

    cfg1.set_modem_id(1);
    cfg2.set_modem_id(2);

    cfg1.set_serial_port("/tmp/ttyvmodem0");
    cfg2.set_serial_port("/tmp/ttyvmodem1");

    std::vector<int> tests_to_run;
    tests_to_run.push_back(4);
    tests_to_run.push_back(5);

    goby::test::acomms::DriverTester tester(driver1, driver2, cfg1, cfg2, tests_to_run,
                                            goby::acomms::protobuf::DRIVER_POPOTO);
    return tester.run();
}
