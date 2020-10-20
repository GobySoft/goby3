// Copyright 2020:
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

#include "goby/middleware/frontseat/groups.h"
#include "goby/middleware/protobuf/frontseat.pb.h"

#include "frontseat_interface.h"

using goby::glog;
namespace frontseat = goby::middleware::frontseat;

void* goby::apps::zeromq::FrontSeatInterface::driver_library_handle_ = 0;

namespace goby
{
namespace apps
{
namespace zeromq
{
class FrontseatInterfaceConfigurator
    : public goby::middleware::ProtobufConfigurator<protobuf::FrontSeatInterfaceConfig>
{
  public:
    FrontseatInterfaceConfigurator(int argc, char* argv[])
        : goby::middleware::ProtobufConfigurator<protobuf::FrontSeatInterfaceConfig>(argc, argv)
    {
        protobuf::FrontSeatInterfaceConfig& cfg = mutable_cfg();

        if (cfg.app().simulation().time().use_sim_time())
            cfg.mutable_frontseat_cfg()->set_sim_warp_factor(
                cfg.app().simulation().time().warp_factor());
    }
};
} // namespace zeromq
} // namespace apps
} // namespace goby

int main(int argc, char* argv[])
{
    // load plugin driver from environmental variable FRONTSEAT_DRIVER_LIBRARY
    char* driver_lib_path = getenv("FRONTSEAT_DRIVER_LIBRARY");
    if (driver_lib_path)
    {
        std::cerr << "Loading frontseat driver library: " << driver_lib_path << std::endl;
        goby::apps::zeromq::FrontSeatInterface::driver_library_handle_ =
            dlopen(driver_lib_path, RTLD_LAZY);
        if (!goby::apps::zeromq::FrontSeatInterface::driver_library_handle_)
        {
            std::cerr << "Failed to open library: " << driver_lib_path << std::endl;
            exit(EXIT_FAILURE);
        }
    }
    else
    {
        std::cerr << "Environmental variable FRONTSEAT_DRIVER_LIBRARY must be set with name of "
                     "the dynamic library containing the specific driver to use."
                  << std::endl;
        exit(EXIT_FAILURE);
    }

    return goby::run<goby::apps::zeromq::FrontSeatInterface>(
        goby::apps::zeromq::FrontseatInterfaceConfigurator(argc, argv));
}

frontseat::InterfaceBase*
load_driver(const goby::apps::zeromq::protobuf::FrontSeatInterfaceConfig& cfg)
{
    typedef frontseat::InterfaceBase* (*driver_load_func)(frontseat::protobuf::Config*);
    driver_load_func driver_load_ptr = (driver_load_func)dlsym(
        goby::apps::zeromq::FrontSeatInterface::driver_library_handle_, "frontseat_driver_load");

    if (!driver_load_ptr)
    {
        glog.is_die() && glog << "Function frontseat_driver_load in library defined in "
                                 "FRONTSEAT_DRIVER_LIBRARY does not exist."
                              << std::endl;
        // suppress clang static analyzer false positive
        exit(EXIT_FAILURE);
    }

    auto frontseat_cfg = cfg.frontseat_cfg();
    frontseat_cfg.set_name(cfg.interprocess().platform());
    frontseat_cfg.mutable_origin()->set_lat_with_units(cfg.app().geodesy().lat_origin_with_units());
    frontseat_cfg.mutable_origin()->set_lon_with_units(cfg.app().geodesy().lon_origin_with_units());

    frontseat::InterfaceBase* driver = (*driver_load_ptr)(&frontseat_cfg);

    if (!driver)
    {
        glog.is_die() && glog << "Function frontseat_driver_load in library defined in "
                                 "FRONTSEAT_DRIVER_LIBRARY returned a null pointer."
                              << std::endl;
        // suppress clang static analyzer false positive
        exit(EXIT_FAILURE);
    }
    return driver;
}

goby::apps::zeromq::FrontSeatInterface::FrontSeatInterface()
    : goby::zeromq::MultiThreadApplication<protobuf::FrontSeatInterfaceConfig>(
          10 * boost::units::si::hertz),
      frontseat_(load_driver(cfg()))
{
    glog.is_debug1() && glog << "Setup subscriptions" << std::endl;
    setup_subscriptions();

    launch_helm_interface();

    glog.is_debug1() && glog << "Launch timer thread" << std::endl;
    launch_timer<STATUS_TIMER>(
        1.0 / boost::units::quantity<boost::units::si::time>(
                  cfg().frontseat_cfg().status_period_with_units()),
        [this]() {
            glog.is_debug1() && glog << "Status: " << frontseat_->status().ShortDebugString()
                                     << std::endl;
            interprocess().publish<frontseat::groups::status>(frontseat_->status());
        });
}

void goby::apps::zeromq::FrontSeatInterface::loop()
{
    frontseat_->do_work();

    if (cfg().frontseat_cfg().exit_on_error() &&
        (frontseat_->state() == frontseat::protobuf::INTERFACE_FS_ERROR ||
         frontseat_->state() == frontseat::protobuf::INTERFACE_HELM_ERROR))
    {
        glog.is_debug1() &&
            glog << "Error state detected and `exit_on_error` == true, so quitting. Bye!"
                 << std::endl;
        quit();
    }
}

void goby::apps::zeromq::FrontSeatInterface::setup_subscriptions()
{
    // helm state
    interprocess().subscribe<frontseat::groups::helm_state>(
        [this](const frontseat::protobuf::HelmStateReport& helm_state) {
            frontseat_->set_helm_state(helm_state.state());
        });

    // commands
    interprocess().subscribe<frontseat::groups::command_request>(
        [this](const frontseat::protobuf::CommandRequest& command) {
            if (frontseat_->state() != frontseat::protobuf::INTERFACE_COMMAND)
                glog.is_debug1() &&
                    glog << "Not sending command because the interface is not in the command state"
                         << std::endl;
            else
                frontseat_->send_command_to_frontseat(command);
        });
    frontseat_->signal_command_response.connect(
        [this](const frontseat::protobuf::CommandResponse& response) {
            interprocess().publish<frontseat::groups::command_response>(response);
        });

    // shortcut for common desired course command
    interprocess().subscribe<frontseat::groups::desired_course, frontseat::protobuf::DesiredCourse>(
        [this](const frontseat::protobuf::DesiredCourse& desired_course) {
            if (frontseat_->state() != frontseat::protobuf::INTERFACE_COMMAND)
                glog.is_debug1() && glog << "Not sending command because the interface is "
                                            "not in the command state"
                                         << std::endl;
            else
            {
                frontseat::protobuf::CommandRequest command;
                *command.mutable_desired_course() = desired_course;
                frontseat_->send_command_to_frontseat(command);
            }
        });

    // data
    interprocess().subscribe<frontseat::groups::data_to_frontseat>(
        [this](const frontseat::protobuf::InterfaceData& data) {
            if (frontseat_->state() != frontseat::protobuf::INTERFACE_COMMAND &&
                frontseat_->state() != frontseat::protobuf::INTERFACE_LISTEN)
                glog.is_debug1() && glog << "Not sending data because the interface is not in the "
                                            "command or listen state"
                                         << std::endl;
            else
                frontseat_->send_data_to_frontseat(data);
        });
    frontseat_->signal_data_from_frontseat.connect(
        [this](const frontseat::protobuf::InterfaceData& data) {
            interprocess().publish<frontseat::groups::data_from_frontseat>(data);
            if (data.has_node_status())
                interprocess().publish<frontseat::groups::node_status>(data.node_status());
        });

    // raw
    interprocess().subscribe<frontseat::groups::raw_send_request>(
        [this](const frontseat::protobuf::Raw& data) {
            if (frontseat_->state() != frontseat::protobuf::INTERFACE_COMMAND &&
                frontseat_->state() != frontseat::protobuf::INTERFACE_LISTEN)
                glog.is_debug1() && glog << "Not sending raw because the interface is not in "
                                            "the command or listen state"
                                         << std::endl;
            else
                frontseat_->send_raw_to_frontseat(data);
        });

    frontseat_->signal_raw_from_frontseat.connect([this](const frontseat::protobuf::Raw& data) {
        interprocess().publish<frontseat::groups::raw_in>(data);
    });
    frontseat_->signal_raw_to_frontseat.connect([this](const frontseat::protobuf::Raw& data) {
        interprocess().publish<frontseat::groups::raw_out>(data);
    });
}
