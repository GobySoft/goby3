// Copyright 2011-2023:
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

#include <algorithm>     // for max, copy
#include <atomic>        // for atomic
#include <chrono>        // for seconds
#include <cstdlib>       // for getenv
#include <cstring>       // for strcpy
#include <dlfcn.h>       // for dlclose
#include <functional>    // for function
#include <map>           // for operat...
#include <memory>        // for allocator
#include <ostream>       // for basic_...
#include <stdexcept>     // for runtim...
#include <string>        // for string
#include <type_traits>   // for __succ...
#include <unistd.h>      // for usleep
#include <unordered_map> // for operat...
#include <vector>        // for vector

#include <Wt/WFlags>                                 // for Wt
#include <Wt/WGlobal>                                // for Applic...
#include <Wt/WIOService>                             // for WIOSer...
#include <Wt/WServer>                                // for WServer
#include <boost/algorithm/string/classification.hpp> // for is_any...
#include <boost/algorithm/string/split.hpp>          // for split
#include <boost/filesystem.hpp>                      // for direct...
#include <boost/iterator/iterator_facade.hpp>        // for iterat...
#include <boost/lexical_cast/bad_lexical_cast.hpp>   // for bad_le...
#include <boost/system/error_code.hpp>               // for error_...
#include <boost/units/quantity.hpp>                  // for operator/
#include <google/protobuf/descriptor.h>              // for Descri...

#include "goby/middleware/marshalling/protobuf.h"

#include "dccl/dynamic_protobuf_manager.h"                    // for Dynami...
#include "goby/middleware/application/configuration_reader.h" // for Config...
#include "goby/middleware/application/interface.h"            // for run
#include "goby/middleware/protobuf/app_config.pb.h"           // for AppConfig
#include "goby/time/steady_clock.h"                           // for Steady...
#include "goby/util/as.h"                                     // for as
#include "goby/util/debug_logger/flex_ostream.h"              // for operat...
#include "goby/util/debug_logger/flex_ostreambuf.h"           // for DIE
#include "goby/zeromq/protobuf/liaison_config.pb.h"           // for Liaiso...
#ifdef LIAISON_STANDALONE
#include "goby/middleware/application/multi_thread.h"
#else
#include "goby/zeromq/application/multi_thread.h" // for MultiThreadApp...
#endif
#include "goby/util/dccl_compat.h"

#include "liaison_wt_thread.h" // for Liaiso...

namespace Wt
{
class WApplication;
class WEnvironment;
} // namespace Wt

using goby::glog;
using namespace Wt;
using namespace goby::util::logger;

namespace goby
{
namespace apps
{
namespace zeromq
{
#ifdef LIAISON_STANDALONE
class Liaison : public goby::middleware::MultiThreadStandaloneApplication<protobuf::LiaisonConfig>
#else
class Liaison : public goby::zeromq::MultiThreadApplication<protobuf::LiaisonConfig>
#endif
{
  public:
    Liaison();
    ~Liaison() override
    {
        terminating_ = true;
        wt_server_.stop();
    }

    void loop() override;

  private:
    void load_proto_file(const std::string& path);

    friend class LiaisonWtThread;

  private:
    Wt::WServer wt_server_;
    std::atomic<bool> terminating_{false};
    std::function<void()> expire_sessions_;
};

} // namespace zeromq
} // namespace apps
} // namespace goby

int main(int argc, char* argv[])
{
    // load plugins from environmental variable GOBY_LIAISON_PLUGINS
    char* plugins = getenv("GOBY_LIAISON_PLUGINS");
    if (plugins)
    {
        std::string s_plugins(plugins);
        std::vector<std::string> plugin_vec;
        boost::split(plugin_vec, s_plugins, boost::is_any_of(";:,"));

        for (auto& i : plugin_vec)
        {
            glog.is(VERBOSE) && glog << "Loading liaison plugin library: " << i << std::endl;
            void* handle = dlopen(i.c_str(), RTLD_LAZY);
            if (handle)
                goby::apps::zeromq::LiaisonWtThread::plugin_handles_.push_back(handle);
            else
                glog.is(DIE) && glog << "Failed to open library: " << i << std::endl;
        }
    }

    int return_value = goby::run<goby::apps::zeromq::Liaison>(argc, argv);
    //    dccl::DynamicProtobufManager::protobuf_shutdown();

    for (auto& plugin_handle : goby::apps::zeromq::LiaisonWtThread::plugin_handles_)
        dlclose(plugin_handle);

    return return_value;
}

goby::apps::zeromq::Liaison::Liaison()
//    : MultiThreadApplication<protobuf::LiaisonConfig>(10*boost::units::si::hertz)
{
    // load all shared libraries
    for (int i = 0, n = cfg().load_shared_library_size(); i < n; ++i)
    {
        glog.is(VERBOSE) && glog << "Loading shared library: " << cfg().load_shared_library(i)
                                 << std::endl;

        void* handle =
            dccl::DynamicProtobufManager::load_from_shared_lib(cfg().load_shared_library(i));

        if (!handle)
        {
            glog.is(DIE) &&
                glog << "Failed to load shared library: " << cfg().load_shared_library(i)
                     << " (dlerror: " << dlerror() << ")" << std::endl;
        }
    }

    // load all .proto files
    dccl::DynamicProtobufManager::enable_compilation();
    for (int i = 0, n = cfg().load_proto_file_size(); i < n; ++i)
        load_proto_file(cfg().load_proto_file(i));

    // load all .proto file directories
    for (int i = 0, n = cfg().load_proto_dir_size(); i < n; ++i)
    {
        boost::filesystem::path current_dir(cfg().load_proto_dir(i));

        for (boost::filesystem::directory_iterator iter(current_dir), end; iter != end; ++iter)
        {
#if BOOST_FILESYSTEM_VERSION == 3
            if (iter->path().extension().string() == ".proto")
#else
            if (iter->path().extension() == ".proto")
#endif

                load_proto_file(iter->path().string());
        }
    }

    try
    {
        std::string doc_root;

        boost::system::error_code ec;
        if (cfg().has_docroot())
            doc_root = cfg().docroot();
        else if (boost::filesystem::exists(boost::filesystem::path(GOBY_LIAISON_COMPILED_DOCROOT),
                                           ec))
            doc_root = GOBY_LIAISON_COMPILED_DOCROOT;
        else if (boost::filesystem::exists(boost::filesystem::path(GOBY_LIAISON_INSTALLED_DOCROOT),
                                           ec))
            doc_root = GOBY_LIAISON_INSTALLED_DOCROOT;
        else
            throw(std::runtime_error("No valid docroot found for Goby Liaison. Set docroot to the "
                                     "valid path to what is normally /usr/share/goby/liaison"));

        // create a set of fake argc / argv for Wt::WServer
        std::vector<std::string> wt_argv_vec;
        std::string str = cfg().app().name() + " --docroot " + doc_root + " --http-port " +
                          goby::util::as<std::string>(cfg().http_port()) + " --http-address " +
                          cfg().http_address() + " " + cfg().additional_wt_http_params();
        boost::split(wt_argv_vec, str, boost::is_any_of(" "));

        char* wt_argv[wt_argv_vec.size()];

        glog.is(DEBUG1) && glog << "setting Wt cfg to: " << std::flush;
        for (int i = 0, n = wt_argv_vec.size(); i < n; ++i)
        {
            wt_argv[i] = new char[wt_argv_vec[i].size() + 1];
            strcpy(wt_argv[i], wt_argv_vec[i].c_str());
            glog.is(DEBUG1) && glog << "\t" << wt_argv[i] << std::endl;
        }

        wt_server_.setServerConfiguration(wt_argv_vec.size(), wt_argv);

        // delete our fake argv
        for (int i = 0, n = wt_argv_vec.size(); i < n; ++i) delete[] wt_argv[i];

        wt_server_.addEntryPoint(Wt::Application,
                                 [this](const Wt::WEnvironment& env) -> Wt::WApplication* {
                                     return new LiaisonWtThread(env, this->cfg());
                                 });

        if (!wt_server_.start())
        {
            glog.is(DIE) && glog << "Could not start Wt HTTP server." << std::endl;
        }
    }
    catch (Wt::WServer::Exception& e)
    {
        glog.is(DIE) && glog << "Could not start Wt HTTP server. Exception: " << e.what()
                             << std::endl;
    }

    // clean up sessions if there are none
    // see https://redmine.webtoolkit.eu/boards/2/topics/5614?r=5615#message-5615
    expire_sessions_ = [=]() {
        int seconds = 10;
        auto start = goby::time::SteadyClock::now();
        while (!terminating_ &&
               goby::time::SteadyClock::now() < start + std::chrono::seconds(seconds))
        {
            // 100 ms to avoid pegging CPU but be responsive to shutdown
            usleep(100000);
        }

        glog.is_debug3() && glog << std::chrono::duration_cast<std::chrono::seconds>(
                                        goby::time::SteadyClock::now().time_since_epoch())
                                        .count()
                                 << ": Expire sessions" << std::endl;

        wt_server_.expireSessions();

        if (!terminating_)
            Wt::WServer::instance()->ioService().post(expire_sessions_);
    };

    Wt::WServer::instance()->ioService().post(expire_sessions_);
}

void goby::apps::zeromq::Liaison::load_proto_file(const std::string& path)
{
#if BOOST_FILESYSTEM_VERSION == 3
    boost::filesystem::path bpath = boost::filesystem::absolute(path);
#else
    boost::filesystem::path bpath = boost::filesystem::complete(path);
#endif
    bpath.normalize();

    glog.is(VERBOSE) && glog << "Loading protobuf file: " << bpath << std::endl;

#ifdef DCCL_VERSION_4_1_OR_NEWER
    if (!dccl::DynamicProtobufManager::user_descriptor_pool_call(
            &google::protobuf::DescriptorPool::FindFileByName, bpath.string()))
        glog.is(DIE) && glog << "Failed to load file." << std::endl;
#else
    if (!dccl::DynamicProtobufManager::user_descriptor_pool().FindFileByName(bpath.string()))
        glog.is(DIE) && glog << "Failed to load file." << std::endl;
#endif
}

void goby::apps::zeromq::Liaison::loop()
{
    glog.is(DEBUG1) && glog << "Liaison loop()" << std::endl;

    // static int i = 0;
    // i++;
    // if(i > (20 * this->loop_frequency_hertz()))
    // {
    //     wt_server_.stop();
    //     quit();
    // }
}
