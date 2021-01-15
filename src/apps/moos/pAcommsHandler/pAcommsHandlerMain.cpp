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

#include "pAcommsHandler.h"

std::vector<void*> plugin_handles_;

using goby::glog;
using namespace goby::util::logger;

int main(int argc, char* argv[])
{
    goby::glog.add_group("pAcommsHandler", goby::util::Colors::yellow);

    // load plugins from environmental variable
    char* plugins = getenv("PACOMMSHANDLER_PLUGINS");
    if (plugins)
    {
        std::string s_plugins(plugins);
        std::vector<std::string> plugin_vec;
        boost::split(plugin_vec, s_plugins, boost::is_any_of(";:,"));

        for (auto& i : plugin_vec)
        {
            std::cout << "Loading pAcommsHandler plugin library: " << i << std::endl;
            void* handle = dlopen(i.c_str(), RTLD_LAZY);

            if (handle)
                plugin_handles_.push_back(handle);
            else
            {
                std::cerr << "Failed to open library: " << i << std::endl;
                exit(EXIT_FAILURE);
            }

            const auto name_function = (const char* (*)(void))dlsym(handle, "goby_driver_name");
            if (name_function)
                goby::apps::moos::CpAcommsHandler::driver_plugins_.insert(
                    std::make_pair(std::string((*name_function)()), handle));
        }
    }

    int return_value = goby::moos::run<goby::apps::moos::CpAcommsHandler>(argc, argv);

    goby::moos::transitional::DCCLAlgorithmPerformer::deleteInstance();
    goby::apps::moos::CpAcommsHandler::delete_instance();
    dccl::DynamicProtobufManager::protobuf_shutdown();

    for (auto& plugin_handle : plugin_handles_) dlclose(plugin_handle);

    return return_value;
}
