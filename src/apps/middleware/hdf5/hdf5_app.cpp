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

#include <cstdlib>
#include <dlfcn.h>
#include <iostream>

#include "goby/util/debug_logger.h"

#include "goby/middleware/log/hdf5/hdf5.h"

void* plugin_handle = 0;

using namespace goby::util::logger;
using goby::glog;

namespace goby
{
namespace middleware
{
namespace hdf5
{
class WriterApp : public goby::middleware::Application<goby::middleware::protobuf::HDF5Config>
{
  public:
    WriterApp() : writer_(app_cfg().output_file(), app_cfg().include_string_fields())
    {
        load();
        collect();
        write();
        quit();
    }

  private:
    void load();
    void collect();
    void write() { writer_.write(); }
    void run() {}

  private:
    std::shared_ptr<goby::middleware::HDF5Plugin> plugin_;
    Writer writer_;
};

} // namespace hdf5
} // namespace middleware
} // namespace goby

void goby::middleware::hdf5::WriterApp::load()
{
    typedef goby::middleware::HDF5Plugin* (*plugin_func)(
        const goby::middleware::protobuf::HDF5Config*);
    plugin_func plugin_ptr = (plugin_func)dlsym(plugin_handle, "goby_hdf5_load");

    if (!plugin_ptr)
        glog.is(DIE) &&
            glog << "Function goby_hdf5_load in library defined in GOBY_HDF5_PLUGIN does not exist."
                 << std::endl;
    else
        plugin_.reset((*plugin_ptr)(&app_cfg()));

    if (!plugin_)
        glog.is(DIE) && glog << "Function goby_hdf5_load in library defined in GOBY_HDF5_PLUGIN "
                                "returned a null pointer."
                             << std::endl;
}

void goby::middleware::hdf5::WriterApp::collect()
{
    goby::middleware::HDF5ProtobufEntry entry;
    while (plugin_->provide_entry(&entry))
    {
        writer_.add_entry(entry);
        entry.clear();
    }
}

int main(int argc, char* argv[])
{
    // load plugin driver from environmental variable GOBY_HDF5_PLUGIN
    const char* plugin_path = getenv("GOBY_HDF5_PLUGIN");
    if (plugin_path)
    {
        std::cerr << "Loading plugin library: " << plugin_path << std::endl;
        plugin_handle = dlopen(plugin_path, RTLD_LAZY);
        if (!plugin_handle)
        {
            std::cerr << "Failed to open library: " << plugin_path << std::endl;
            exit(EXIT_FAILURE);
        }
    }
    else
    {
        std::cerr << "Environmental variable GOBY_HDF5_PLUGIN must be set with name of the dynamic "
                     "library containing the specific frontend plugin to use."
                  << std::endl;
        exit(EXIT_FAILURE);
    }

    return goby::run<goby::middleware::hdf5::WriterApp>(argc, argv);
}
