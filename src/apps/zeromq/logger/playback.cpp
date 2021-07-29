// Copyright 2021:
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

#include "goby/middleware/marshalling/protobuf.h"

#include "goby/middleware/log/dccl_log_plugin.h" // for DCCLPl...
#include "goby/middleware/log/log_entry.h"       // for LogEntry
#include "goby/zeromq/application/single_thread.h"
#include "goby/zeromq/protobuf/interprocess_config.pb.h"
#include "goby/zeromq/protobuf/logger_config.pb.h"
#include "goby/zeromq/transport/interprocess.h"

using goby::glog;

namespace goby
{
namespace apps
{
namespace zeromq
{
class Playback : public goby::zeromq::SingleThreadApplication<protobuf::PlaybackConfig>
{
  public:
    Playback()
        : goby::zeromq::SingleThreadApplication<protobuf::PlaybackConfig>(100 *
                                                                          boost::units::si::hertz),
          f_in_(cfg().input_file().c_str()),
          playback_start_(goby::time::SystemClock::now() +
                          goby::time::convert_duration<goby::time::SystemClock::duration>(
                              cfg().playback_start_delay_with_units())),
          group_regex_(cfg().group_regex()),
          internal_group_regex_("goby::zeromq::_internal_.*")
    {
        for (auto filter : cfg().type_filter())
            type_regex_.insert(std::make_pair(filter.scheme(), filter.regex()));

        for (const auto& lib : cfg().load_shared_library())
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

        read_next_entry();
        log_start_ = next_log_entry_.timestamp();
    }

    ~Playback() override
    {
        for (void* handle : dl_handles_) dlclose(handle);
    }

  private:
    void loop() override
    {
        while (is_time_to_publish())
        {
            // playback the entry
            if (!is_filtered())
            {
                glog.is_verbose() && glog << "Playing back: " << next_log_entry_.scheme() << " | "
                                          << next_log_entry_.group() << " | "
                                          << next_log_entry_.type() << " | "
                                          << goby::time::convert<boost::posix_time::ptime>(
                                                 next_log_entry_.timestamp())
                                          << std::endl;

                std::vector<char> data(next_log_entry_.data().begin(),
                                       next_log_entry_.data().end());

                interprocess().publish_serialized(next_log_entry_.type(), next_log_entry_.scheme(),
                                                  data, next_log_entry_.group());
            }

            // read the next entry
            read_next_entry();
        }

        if (do_quit_)
            quit();
    }

    void read_next_entry()
    {
        try
        {
            next_log_entry_.parse(&f_in_);
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
            else
                glog.is_verbose() && glog << "Reached EOF" << std::endl;
            do_quit_ = true;
        }
    }

    bool is_time_to_publish()
    {
        if (do_quit_)
            return false;

        auto now = goby::time::SystemClock::now();
        auto dt_log = next_log_entry_.timestamp() - log_start_;
        auto dt_wall = now - playback_start_;
        // as much wall time has elapsed (modified by rate) as log time
        return (dt_wall * cfg().rate()) >= dt_log;
    }

    bool is_filtered()
    {
        std::string group = next_log_entry_.group();
        bool internal_group_is_filtered = std::regex_match(group, internal_group_regex_);

        // check regex and type_filters
        bool group_is_filtered = true;
        if (std::regex_match(group, group_regex_))
            group_is_filtered = false;

        bool type_is_filtered = true;
        if (!type_regex_.empty())
        {
            auto it_p = type_regex_.equal_range(next_log_entry_.scheme());
            for (auto it = it_p.first; it != it_p.second; ++it)
            {
                if (std::regex_match(next_log_entry_.type(), it->second))
                    type_is_filtered = false;
            }
        }
        else
        {
            // if no type_filters are set, allow all messages
            type_is_filtered = false;
        }

        return internal_group_is_filtered || group_is_filtered || type_is_filtered;
    }

  private:
    // dynamically loaded libraries
    std::vector<void*> dl_handles_;

    // scheme to plugin
    std::map<int, std::unique_ptr<goby::middleware::log::LogPlugin>> plugins_;

    std::ifstream f_in_;

    goby::middleware::log::LogEntry next_log_entry_;

    goby::time::SystemClock::time_point log_start_;
    goby::time::SystemClock::time_point playback_start_;

    std::regex group_regex_;
    std::regex internal_group_regex_;
    std::multimap<int, std::regex> type_regex_;

    bool do_quit_{false};
};
} // namespace zeromq
} // namespace apps
} // namespace goby

int main(int argc, char* argv[]) { return goby::run<goby::apps::zeromq::Playback>(argc, argv); }
