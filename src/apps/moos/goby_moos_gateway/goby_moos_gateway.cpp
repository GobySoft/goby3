// Copyright 2009-2017 Toby Schneider (http://gobysoft.org/index.wt/people/toby)
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

#include "goby/moos/middleware/moos_plugin_translator.h"
#include "goby/middleware/multi-thread-application.h"

#include "goby/moos/protobuf/moos_gateway_config.pb.h"

using AppBase = goby::MultiThreadApplication<GobyMOOSGatewayConfig>;
using ThreadBase = goby::SimpleThread<GobyMOOSGatewayConfig>;


class GobyMOOSGateway : public AppBase
{
public:
    GobyMOOSGateway()
        {
            for(const std::string& lib_path : cfg().plugin_library())
            {
                // LOAD PLUGIN LIBRARIES
            }            
        }
};

int main(int argc, char* argv[])
{ return goby::run<GobyMOOSGateway>(argc, argv); }
