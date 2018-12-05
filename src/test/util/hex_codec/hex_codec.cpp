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

#include "goby/util/binary.h"

#include <iomanip>
#include <iostream>
#include <sstream>

int main()
{
    using goby::util::hex_decode;
    using goby::util::hex_encode;

    {
        std::cout << "testing lowercase even" << std::endl;

        std::stringstream ss;
        for (int i = 0; i <= 255; ++i) ss << std::setfill('0') << std::setw(2) << std::hex << i;

        std::string hex1 = ss.str();
        std::string hex2;

        std::string bytes;
        hex_decode(hex1, &bytes);
        hex_encode(bytes, &hex2);

        std::cout << "hex1: " << hex1 << std::endl;
        std::cout << "hex2: " << hex2 << std::endl;
        assert(hex1 == hex2.substr(hex2.size() - hex1.size()));
    }
    {
        std::cout << "testing uppercase even" << std::endl;

        std::stringstream ss;
        for (int i = 0; i <= 255; ++i)
            ss << std::uppercase << std::setfill('0') << std::setw(2) << std::hex << i;

        std::string hex1 = ss.str();
        std::string hex2;

        std::string bytes;
        hex_decode(hex1, &bytes);
        hex_encode(bytes, &hex2, true);

        std::cout << "hex1: " << hex1 << std::endl;
        std::cout << "hex2: " << hex2 << std::endl;
        assert(hex1 == hex2.substr(hex2.size() - hex1.size()));
    }

    {
        std::cout << "testing lowercase odd" << std::endl;

        std::stringstream ss;
        for (int i = 0; i <= 255; ++i) ss << std::setfill('0') << std::setw(2) << std::hex << i;

        ss << "d";

        std::string hex1 = ss.str();
        std::string hex2;

        std::string bytes;
        hex_decode(hex1, &bytes);
        hex_encode(bytes, &hex2);

        std::cout << "hex1: " << hex1 << std::endl;
        std::cout << "hex2: " << hex2 << std::endl;
        assert(hex1 == hex2.substr(hex2.size() - hex1.size()));
    }
    {
        std::cout << "testing uppercase odd" << std::endl;

        std::stringstream ss;
        for (int i = 0; i <= 255; ++i)
            ss << std::uppercase << std::setfill('0') << std::setw(2) << std::hex << i;
        ss << "D";

        std::string hex1 = ss.str();
        std::string hex2;

        std::string bytes;
        hex_decode(hex1, &bytes);
        hex_encode(bytes, &hex2, true);

        std::cout << "hex1: " << hex1 << std::endl;
        std::cout << "hex2: " << hex2 << std::endl;
        assert(hex1 == hex2.substr(hex2.size() - hex1.size()));
    }

    std::cout << "all tests passed" << std::endl;

    return 0;
}
