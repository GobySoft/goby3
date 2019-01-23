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

#include "goby/common/application_base3.h"

#include "goby/middleware/log.h"
#include "goby/middleware/protobuf/log_tool_config.pb.h"

#include "dccl_logger_plugin.h"
#include "protobuf_logger_plugin.h"

using goby::glog;

namespace goby
{
class LogTool : public goby::common::ApplicationBase3<protobuf::LogToolConfig>
{
  public:
    LogTool();
    ~LogTool()
    {
        dccl::DynamicProtobufManager::protobuf_shutdown();
        for (void* handle : dl_handles_) dlclose(handle);
    }

  private:
    // never gets called
    void run() override {}

    // dynamically loaded libraries
    std::vector<void*> dl_handles_;

    // scheme to plugin
    std::map<int, std::unique_ptr<logger::LogPlugin> > plugins_;

    std::ifstream f_in_;
    std::ofstream f_out_;
};

} // namespace goby

int main(int argc, char* argv[]) { return goby::run<goby::LogTool>(argc, argv); }

goby::LogTool::LogTool()
    : f_in_(app_cfg().input_file().c_str()),
      f_out_(app_cfg().output_file() == "-" ? "/dev/stdout" : app_cfg().output_file().c_str())
{
    for (const auto& lib : app_cfg().load_shared_library())
    {
        void* lib_handle = dlopen(lib.c_str(), RTLD_LAZY);
        if (!lib_handle)
            glog.is_die() && glog << "Failed to open library: " << lib << std::endl;
        dl_handles_.push_back(lib_handle);
    }

    plugins_[goby::MarshallingScheme::PROTOBUF].reset(new logger::ProtobufPlugin);
    plugins_[goby::MarshallingScheme::DCCL].reset(new logger::DCCLPlugin);

    for (auto& p : plugins_) p.second->register_read_hooks(f_in_);

    while (true)
    {
        try
        {
            goby::LogEntry log_entry;
            log_entry.parse(&f_in_);
            try
            {
                if (log_entry.scheme() == goby::MarshallingScheme::DCCL) {}
                else if (log_entry.scheme() == goby::MarshallingScheme::PROTOBUF)
                {
                    auto desc = dccl::DynamicProtobufManager::find_descriptor(log_entry.type());

                    if (!desc)
                        throw(logger::LogException(
                            "Failed to find Descriptor for Protobuf message of type: " +
                            log_entry.type()));

                    auto msg = dccl::DynamicProtobufManager::new_protobuf_message<
                        std::unique_ptr<google::protobuf::Message> >(desc);

                    msg->ParseFromArray(&log_entry.data()[0], log_entry.data().size());

                    f_out_ << log_entry.scheme() << " | " << log_entry.group() << " | "
                           << log_entry.type() << " | " << msg->ShortDebugString() << std::endl;
                }
            }

            catch (logger::LogException& e)
            {
                glog.is_warn() && glog << "Failed to parse message (scheme: " << log_entry.scheme()
                                       << ", group: " << log_entry.group()
                                       << ", type: " << log_entry.type() << std::endl;

                f_out_ << log_entry.scheme() << " | " << log_entry.group() << " | "
                       << log_entry.type() << " | "
                       << "Unable to parse message of " << log_entry.data().size()
                       << " bytes. Reason: " << e.what() << std::endl;
            }
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
