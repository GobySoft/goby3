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
#include "goby/middleware/gobyd/groups.h"
#include "goby/middleware/protobuf/intervehicle_status.pb.h"
#include "goby/middleware/transport-interprocess-zeromq.h"
#include "goby/middleware/transport-intervehicle.h"

#include "goby/middleware/protobuf/gobyd_config.pb.h"

#include "goby/middleware/terminate/terminate.h"

using namespace goby::common::logger;
using goby::glog;

namespace goby
{
class Daemon : public goby::common::ApplicationBase3<goby::protobuf::GobyDaemonConfig>
{
  public:
    Daemon();
    ~Daemon();

  private:
    void run() override;

  private:
    // for handling ZMQ Interprocess Communications
    std::unique_ptr<zmq::context_t> router_context_;
    std::unique_ptr<zmq::context_t> manager_context_;
    goby::ZMQRouter router_;
    goby::ZMQManager manager_;
    std::unique_ptr<std::thread> router_thread_;
    std::unique_ptr<std::thread> manager_thread_;

    // For hosting an InterVehiclePortal
    std::unique_ptr<InterProcessPortal<> > interprocess_;
    std::unique_ptr<InterVehiclePortal<InterProcessPortal<> > > intervehicle_;
};
} // namespace goby

int main(int argc, char* argv[]) { return goby::run<goby::Daemon>(argc, argv); }

goby::Daemon::Daemon()
    : router_context_(new zmq::context_t(app_cfg().router_threads())),
      manager_context_(new zmq::context_t(1)),
      router_(*router_context_, app_cfg().interprocess()),
      manager_(*manager_context_, app_cfg().interprocess(), router_),
      router_thread_(new std::thread([&] { router_.run(); })),
      manager_thread_(new std::thread([&] { manager_.run(); }))
{
    if (!app_cfg().interprocess().has_platform())
    {
        glog.is(WARN) && glog << "Using default platform name of "
                              << app_cfg().interprocess().platform() << std::endl;
    }

    interprocess_.reset(new InterProcessPortal<>(app_cfg().interprocess()));
    if (app_cfg().has_intervehicle())
        intervehicle_.reset(new InterVehiclePortal<InterProcessPortal<> >(
            *interprocess_, app_cfg().intervehicle()));

    // handle goby_terminate request
    interprocess_->subscribe<groups::terminate_request, protobuf::TerminateRequest>(
        [this](const protobuf::TerminateRequest& request) {
            bool match = false;
            protobuf::TerminateResponse resp;
            std::tie(match, resp) =
                goby::terminate::check_terminate(request, app_cfg().app().name());
            if (match)
            {
                interprocess_->publish<groups::terminate_response>(resp);
                // as gobyd mediates all interprocess() comms; wait for a bit to hopefully get our response out before shutting down
                sleep(1);
                quit();
            }
        });
}

goby::Daemon::~Daemon()
{
    manager_context_.reset();
    router_context_.reset();
    manager_thread_->join();
    router_thread_->join();
}

void goby::Daemon::run()
{
    if (intervehicle_)
    {
        intervehicle_->poll(std::chrono::milliseconds(200));

        protobuf::InterVehicleStatus status;
        status.set_tx_queue_size(intervehicle_->tx_queue_size());

        interprocess_->publish<groups::intervehicle_outbound>(status);
    }
    else
    {
        interprocess_->poll();
    }
}
