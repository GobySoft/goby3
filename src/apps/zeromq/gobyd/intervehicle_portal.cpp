// Copyright 2023:
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

#include "goby/middleware/application/configuration_reader.h" // for Config...
#include "goby/middleware/application/configurator.h"         // for Protob...
#include "goby/middleware/application/detail/interprocess_common.h"
#include "goby/middleware/application/interface.h" // for run
#include "goby/middleware/coroner/functions.h"
#include "goby/middleware/protobuf/app_config.pb.h"   // for AppConfig
#include "goby/middleware/protobuf/intervehicle.pb.h" // for Repeat...
#include "goby/middleware/protobuf/terminate.pb.h"    // for Termin...
#include "goby/middleware/terminate/groups.h"         // for termin...
#include "goby/middleware/terminate/terminate.h"      // for check_...
#include "goby/middleware/transport/interthread.h"    // for InterT...
#include "goby/middleware/transport/intervehicle.h"   // for InterV...
#include "goby/util/debug_logger.h"
#include "goby/zeromq/protobuf/gobyd_config.pb.h"
#include "goby/zeromq/transport/interprocess.h"

#include "common.h"

using goby::glog;

namespace goby
{
namespace apps
{
namespace zeromq
{
class IntervehiclePortal
    : public goby::middleware::Application<protobuf::GobyIntervehiclePortalConfig>
{
  public:
    IntervehiclePortal();
    ~IntervehiclePortal() override;

  private:
    void run() override;

    void thread_health(goby::middleware::protobuf::ThreadHealth& health)
    {
        health.set_name("main");
        health.set_state(goby::middleware::protobuf::HEALTH__OK);
    }

  private:
    // For hosting an InterVehiclePortal
    goby::middleware::InterThreadTransporter interthread_;
    goby::zeromq::InterProcessPortal<goby::middleware::InterThreadTransporter> interprocess_;
    goby::middleware::InterVehiclePortal<decltype(interprocess_)> intervehicle_;

    template <typename AppType, typename Transporter>
    friend void middleware::coroner::subscribe_process_health_request(
        AppType* this_app, Transporter& transporter,
        std::function<void(std::shared_ptr<middleware::protobuf::ProcessHealth>& ph)> preseed_hook);

    template <typename AppType, typename InterProcessTransporter>
    friend void middleware::terminate::subscribe_process_terminate_request(
        AppType* this_app, InterProcessTransporter& interprocess, bool do_quit);
};

class IntervehiclePortalConfigurator
    : public goby::middleware::ProtobufConfigurator<protobuf::GobyIntervehiclePortalConfig>
{
  public:
    IntervehiclePortalConfigurator(int argc, char* argv[])
        : goby::middleware::ProtobufConfigurator<protobuf::GobyIntervehiclePortalConfig>(argc, argv)
    {
        protobuf::GobyIntervehiclePortalConfig& cfg = mutable_cfg();
        process_intervehicle_config(cfg);
    }
};

} // namespace zeromq
} // namespace apps
} // namespace goby

int main(int argc, char* argv[])
{
    return goby::run<goby::apps::zeromq::IntervehiclePortal>(
        goby::apps::zeromq::IntervehiclePortalConfigurator(argc, argv));
}

goby::apps::zeromq::IntervehiclePortal::IntervehiclePortal()
    : interprocess_(goby::middleware::detail::make_interprocess_config(
          this->app_cfg().interprocess(), this->app_name())),
      intervehicle_(interprocess_, app_cfg().intervehicle())
{
    middleware::terminate::subscribe_process_terminate_request(this, interprocess_);
    middleware::coroner::subscribe_process_health_request(this, interprocess_);

    glog.is_verbose() && glog << "=== goby_intervehicle_portal is ready ===" << std::endl;
    interprocess_.ready();
}

goby::apps::zeromq::IntervehiclePortal::~IntervehiclePortal() {}

void goby::apps::zeromq::IntervehiclePortal::run() { intervehicle_.poll(); }
