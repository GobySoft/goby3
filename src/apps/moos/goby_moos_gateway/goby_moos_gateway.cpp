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

#include <dlfcn.h>

#include "goby/moos/middleware/moos_plugin_translator.h"
#include "goby/zeromq/multi-thread-application.h"

#include "goby/moos/protobuf/moos_gateway_config.pb.h"

using goby::apps::moos::protobuf::GobyMOOSGatewayConfig;
using AppBase = goby::zeromq::MultiThreadApplication<GobyMOOSGatewayConfig>;
using ThreadBase = goby::middleware::SimpleThread<GobyMOOSGatewayConfig>;

using goby::glog;
using namespace goby::util::logger;

namespace goby
{
namespace moos
{
class GobyMOOSGateway : public AppBase
{
  public:
    GobyMOOSGateway()
    {
        for (const std::string& lib_path : cfg().plugin_library())
        {
            glog.is(VERBOSE) && glog << "Loading shared library: " << lib_path << std::endl;

            void* handle = dlopen(lib_path.c_str(), RTLD_LAZY);

            if (!handle)
            {
                glog.is(DIE) &&
                    glog << "Failed loading shared library: " << lib_path
                         << ". Check path provided or add to /etc/ld.so.conf or LD_LIBRARY_PATH"
                         << std::endl;
            }
            else
            {
                dl_handles_.push_back(handle);
                using plugin_load_func = void (*)(AppBase*);
                plugin_load_func load_ptr =
                    (plugin_load_func)dlsym(handle, "goby3_moos_gateway_load");

                if (!load_ptr)
                    glog.is(DIE) && glog << "Function goby3_moos_gateway_load in library: "
                                         << lib_path << " does not exist." << std::endl;

                (*load_ptr)(static_cast<AppBase*>(this));
            }
        }
    }

    ~GobyMOOSGateway()
    {
        for (void* handle : dl_handles_)
        {
            using plugin_unload_func = void (*)(AppBase*);
            plugin_unload_func unload_ptr =
                (plugin_unload_func)dlsym(handle, "goby3_moos_gateway_unload");

            if (unload_ptr)
                (*unload_ptr)(static_cast<AppBase*>(this));

            dlclose(handle);
        }
    }

  private:
    std::vector<void*> dl_handles_;
};
} // namespace moos
} // namespace goby

int main(int argc, char* argv[]) { return goby::run<goby::moos::GobyMOOSGateway>(argc, argv); }
