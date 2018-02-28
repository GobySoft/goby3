// Copyright 2009-2018 Toby Schneider (http://gobysoft.org/index.wt/people/toby)
//                     GobySoft, LLC (2013-)
//                     Massachusetts Institute of Technology (2007-2014)
//                     Community contributors (see AUTHORS file)
//
//
// This file is part of the Goby Underwater Autonomy Project Libraries
// ("The Goby Libraries").
//
// The Goby Libraries are free software: you can redistribute them and/or modify
// them under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 2.1 of the License, or
// (at your option) any later version.
//
// The Goby Libraries are distributed in the hope that they will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with Goby.  If not, see <http://www.gnu.org/licenses/>.

#ifndef VERSION20110304H
#define VERSION20110304H

#include <string>
#include <sstream>

#define GOBY_VERSION_MAJOR @GOBY_VERSION_MAJOR@
#define GOBY_VERSION_MINOR @GOBY_VERSION_MINOR@
#define GOBY_VERSION_PATCH @GOBY_VERSION_PATCH@

namespace goby
{
    const std::string VERSION_STRING = "@GOBY_VERSION@";
    const std::string VERSION_DATE = "@GOBY_VERSION_DATE@";

    inline std::string version_message()
    {
        std::stringstream ss;
        ss << "This is Version " << goby::VERSION_STRING
           << " of the Goby Underwater Autonomy Project released on "
           << goby::VERSION_DATE
           <<".\n See https://launchpad.net/goby to search for updates.";
        return ss.str();
    }
}

#endif
