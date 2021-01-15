// Copyright 2011-2020:
//   GobySoft, LLC (2013-)
//   Massachusetts Institute of Technology (2007-2014)
//   Community contributors (see AUTHORS file)
// File authors:
//   Toby Schneider <toby@gobysoft.org>
//   Shawn Dooley <shawn@shawndooley.net>
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

#include <algorithm>     // for copy
#include <cstdlib>       // for exit
#include <dlfcn.h>       // for dlsym
#include <exception>     // for exception
#include <iostream>      // for operat...
#include <map>           // for operat...
#include <string>        // for string
#include <unordered_map> // for operat...
#include <vector>        // for vector

#include <boost/algorithm/string/classification.hpp> // for is_any...
#include <boost/algorithm/string/split.hpp>          // for split
#include <boost/units/quantity.hpp>                  // for operator/

#include "goby/middleware/application/configuration_reader.h" // for Config...
#include "goby/middleware/application/interface.h"            // for run
#include "goby/moos/protobuf/moos_gateway_config.pb.h"        // for GobyMO...
#include "goby/util/debug_logger.h"
#include "goby/zeromq/application/multi_thread.h" // for MultiT...

using goby::apps::moos::protobuf::GobyMOOSGatewayConfig;
using AppBase = goby::zeromq::MultiThreadApplication<GobyMOOSGatewayConfig>;

using goby::glog;
using namespace goby::util::logger;

namespace goby
{
namespace moos
{
class GobyMOOSGateway
    : public goby::zeromq::MultiThreadApplication<apps::moos::protobuf::GobyMOOSGatewayConfig>
{
  public:
    GobyMOOSGateway();
    ~GobyMOOSGateway() override;

    static std::vector<void*> dl_handles_;

  private:
};
} // namespace moos
} // namespace goby

std::vector<void*> goby::moos::GobyMOOSGateway::dl_handles_;

int main(int argc, char* argv[])
{
    // load plugins from environmental variable
    char* plugins = getenv("GOBY_MOOS_GATEWAY_PLUGINS");
    if (plugins)
    {
        std::string s_plugins(plugins);
        std::vector<std::string> plugin_vec;
        boost::split(plugin_vec, s_plugins, boost::is_any_of(";:,"));

        for (const auto& plugin : plugin_vec)
        {
            glog.is(VERBOSE) && glog << "Loading plugin library: " << plugin << std::endl;
            void* handle = dlopen(plugin.c_str(), RTLD_LAZY);
            if (handle)
            {
                goby::moos::GobyMOOSGateway::dl_handles_.push_back(handle);
            }
            else
            {
                std::cerr << "Failed to open library: " << plugin << ", reason: " << dlerror()
                          << std::endl;
                exit(EXIT_FAILURE);
            }
            if (!dlsym(handle, "goby3_moos_gateway_load"))
            {
                std::cerr << "Function goby3_moos_gateway_load in library: " << plugin
                          << " does not exist." << std::endl;
                exit(EXIT_FAILURE);
            }
        }
    }
    else
    {
        std::cerr << "Must define at least one plugin library in "
                     "GOBY_MOOS_GATEWAY_PLUGINS environmental variable"
                  << std::endl;
        exit(EXIT_FAILURE);
    }

    return goby::run<goby::moos::GobyMOOSGateway>(argc, argv);
}

goby::moos::GobyMOOSGateway::GobyMOOSGateway()
{
    const auto& app_cfg = this->app_cfg();
    std::cout << "main: &app_cfg_: " << &app_cfg << std::endl;

    for (void* handle : dl_handles_)
    {
        using plugin_load_func = void (*)(AppBase*);
        auto load_ptr = (plugin_load_func)dlsym(handle, "goby3_moos_gateway_load");
        (*load_ptr)(this);
    }
}

goby::moos::GobyMOOSGateway::~GobyMOOSGateway()
{
    for (void* handle : dl_handles_)
    {
        using plugin_unload_func = void (*)(AppBase*);
        auto unload_ptr = (plugin_unload_func)dlsym(handle, "goby3_moos_gateway_unload");

        if (unload_ptr)
            (*unload_ptr)(this);
    }
}
