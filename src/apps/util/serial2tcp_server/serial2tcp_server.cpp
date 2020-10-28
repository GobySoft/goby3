// Copyright 2010-2020:
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

#include <iostream>

#include "goby/util/as.h"
#include "goby/util/linebasedcomms.h"

using goby::util::as;

int main(int argc, char* argv[])
{
    int run_freq = 100;

    if (argc < 4)
    {
        std::cout
            << "usage: serial2tcp_server server_port serial_port serial_baud [run-frequency=100]"
            << std::endl;
        return 1;
    }

    std::string server_port = argv[1];
    std::string serial_port = argv[2];
    std::string serial_baud = argv[3];
    if (argc == 5)
        run_freq = goby::util::as<int>(argv[4]);

    goby::util::TCPServer tcp_server(as<unsigned>(server_port));
    goby::util::SerialClient serial_client(serial_port, as<unsigned>(serial_baud));

    tcp_server.start();
    serial_client.start();

    std::string s;
    for (;;)
    {
        while (serial_client.readline(&s)) tcp_server.write(s);

        while (tcp_server.readline(&s)) serial_client.write(s);

        usleep(1000000 / run_freq);
    }
}
