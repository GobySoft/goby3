// Copyright 2019-2021:
//   GobySoft, LLC (2013-)
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

#include <algorithm>                       // for max
#include <dccl/dynamic_protobuf_manager.h> // for Dynami...
#include <dlfcn.h>                         // for dlclose
#include <map>                             // for map
#include <memory>                          // for unique...
#include <ostream>                         // for operat...
#include <string>                          // for operat...
#include <utility>                         // for pair
#include <vector>                          // for vector

#include <boost/filesystem.hpp> // for path

#include "goby/middleware/application/configuration_reader.h" // for Config...
#include "goby/middleware/application/interface.h"            // for run
#include "goby/middleware/group.h"                            // for operat...
#include "goby/middleware/log/dccl_log_plugin.h"              // for DCCLPl...
#include "goby/middleware/log/log_entry.h"                    // for LogEntry
#include "goby/middleware/log/log_plugin.h"                   // for LogPlugin
#include "goby/middleware/marshalling/interface.h"            // for Marsha...
#include "goby/middleware/protobuf/log_tool_config.pb.h"      // for LogToo...
#include "goby/util/debug_logger/flex_ostream.h"              // for operat...

#ifdef HAS_HDF5
#include "goby/middleware/log/hdf5/hdf5.h" // for Writer
#endif

#include "goby/middleware/log/protobuf_log_plugin.h" // for Protob...

using goby::glog;

namespace goby
{
///\brief Code beloning to applications and other binaries
namespace apps
{
namespace middleware
{
class LogTool : public goby::middleware::Application<protobuf::LogToolConfig>
{
  public:
    LogTool();
    ~LogTool() override
    {
#ifdef HAS_HDF5
        if (app_cfg().format() == protobuf::LogToolConfig::HDF5)
            h5_writer_->write();

        // need to clear these objects before protobuf shutdown or else we get an invalid pointer error
        h5_writer_.reset();
#endif

        dccl::DynamicProtobufManager::protobuf_shutdown();
        for (void* handle : dl_handles_) dlclose(handle);
    }

  private:
    std::string create_output_filename()
    {
        if (app_cfg().has_output_file())
        {
            return app_cfg().output_file() == "-" ? "/dev/stdout" : app_cfg().output_file();
        }
        else
        {
            boost::filesystem::path input_path(app_cfg().input_file());
            std::string output_file = input_path.stem().native();
            switch (app_cfg().format())
            {
                case protobuf::LogToolConfig::DEBUG_TEXT: output_file += ".txt"; break;
                case protobuf::LogToolConfig::HDF5: output_file += ".h5"; break;
            }
            return output_file;
        }
    }

    // never gets called
    void run() override {}

  private:
    // dynamically loaded libraries
    std::vector<void*> dl_handles_;

    // scheme to plugin
    std::map<int, std::unique_ptr<goby::middleware::log::LogPlugin>> plugins_;

    std::ifstream f_in_;
    std::string output_file_path_;

    std::ofstream f_out_;

#ifdef HAS_HDF5
    std::unique_ptr<goby::middleware::hdf5::Writer> h5_writer_;
#endif
};
} // namespace middleware
} // namespace apps
} // namespace goby

int main(int argc, char* argv[]) { return goby::run<goby::apps::middleware::LogTool>(argc, argv); }

goby::apps::middleware::LogTool::LogTool()
    : f_in_(app_cfg().input_file().c_str()), output_file_path_(create_output_filename())
{
    switch (app_cfg().format())
    {
        case protobuf::LogToolConfig::DEBUG_TEXT: f_out_.open(output_file_path_.c_str()); break;
#ifdef HAS_HDF5
        case protobuf::LogToolConfig::HDF5:
            h5_writer_ = std::make_unique<goby::middleware::hdf5::Writer>(output_file_path_);
            break;
#endif
        default:
            glog.is_die() &&
                glog << "Format: " << protobuf::LogToolConfig::OutputFormat_Name(app_cfg().format())
                     << " is not supported. Make sure you have compiled Goby with the correct "
                        "supporting library"
                     << std::endl;
            break;
    }

    for (const auto& lib : app_cfg().load_shared_library())
    {
        void* lib_handle = dlopen(lib.c_str(), RTLD_LAZY);
        if (!lib_handle)
            glog.is_die() && glog << "Failed to open library: " << lib << std::endl;
        dl_handles_.push_back(lib_handle);
    }

    plugins_[goby::middleware::MarshallingScheme::PROTOBUF] =
        std::make_unique<goby::middleware::log::ProtobufPlugin>();
    plugins_[goby::middleware::MarshallingScheme::DCCL] =
        std::make_unique<goby::middleware::log::DCCLPlugin>();

    for (auto& p : plugins_) p.second->register_read_hooks(f_in_);

    while (true)
    {
        try
        {
            goby::middleware::log::LogEntry log_entry;
            log_entry.parse(&f_in_);
            try
            {
                auto plugin = plugins_.find(log_entry.scheme());
                if (plugin == plugins_.end())
                    throw(goby::middleware::log::LogException("No plugin available for scheme: " +
                                                              std::to_string(log_entry.scheme())));

                switch (app_cfg().format())
                {
                    case protobuf::LogToolConfig::DEBUG_TEXT:
                    {
                        auto debug_text_msg = plugin->second->debug_text_message(log_entry);
                        f_out_ << log_entry.scheme() << " | " << log_entry.group() << " | "
                               << log_entry.type() << " | "
                               << goby::time::convert<boost::posix_time::ptime>(
                                      log_entry.timestamp())
                               << " | " << debug_text_msg << std::endl;
                        break;
                    }
                    case protobuf::LogToolConfig::HDF5: {
#ifdef HAS_HDF5
                        auto h5_entries = plugin->second->hdf5_entry(log_entry);
                        for (const auto& entry : h5_entries) h5_writer_->add_entry(entry);
#endif
                        break;
                    }
                }
            }

            catch (goby::middleware::log::LogException& e)
            {
                glog.is_warn() && glog << "Failed to parse message (scheme: " << log_entry.scheme()
                                       << ", group: " << log_entry.group()
                                       << ", type: " << log_entry.type() << std::endl;

                switch (app_cfg().format())
                {
                    case protobuf::LogToolConfig::DEBUG_TEXT:
                        f_out_ << log_entry.scheme() << " | " << log_entry.group() << " | "
                               << log_entry.type() << " | "
                               << "Unable to parse message of " << log_entry.data().size()
                               << " bytes. Reason: " << e.what() << std::endl;
                        break;
                    case protobuf::LogToolConfig::HDF5:
                        // nothing useful to write to the HDF5 file
                        break;
                }
            }
        }
        catch (goby::middleware::log::LogException& e)
        {
            glog.is_warn() && glog << "Exception processing input log (will attempt to continue): "
                                   << e.what() << std::endl;
        }
        catch (std::exception& e)
        {
            if (!f_in_.eof())
                glog.is_warn() && glog << "Error processing input log: " << e.what() << std::endl;

            break;
        }
    }

    quit();
}
