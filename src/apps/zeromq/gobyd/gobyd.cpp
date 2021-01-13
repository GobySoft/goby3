// Copyright 2016-2020:
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

#include <memory>

#include "goby/middleware/application/interface.h"
#include "goby/middleware/gobyd/groups.h"
#include "goby/middleware/protobuf/intervehicle.pb.h"
#include "goby/middleware/transport/intervehicle.h"
#include "goby/zeromq/transport/interprocess.h"

#include "goby/zeromq/protobuf/gobyd_config.pb.h"

#include "goby/middleware/terminate/terminate.h"

using namespace goby::util::logger;
using goby::glog;

namespace goby
{
namespace apps
{
namespace zeromq
{
class Daemon : public goby::middleware::Application<protobuf::GobyDaemonConfig>
{
  public:
    Daemon();
    ~Daemon();

  private:
    void run() override;

    goby::zeromq::Manager make_manager()
    {
        return app_cfg().has_hold()
                   ? goby::zeromq::Manager(*manager_context_, app_cfg().interprocess(), router_,
                                           app_cfg().hold())
                   : goby::zeromq::Manager(*manager_context_, app_cfg().interprocess(), router_);
    }

  private:
    // for handling ZMQ Interprocess Communications
    std::unique_ptr<zmq::context_t> router_context_;
    std::unique_ptr<zmq::context_t> manager_context_;
    goby::zeromq::Router router_;
    goby::zeromq::Manager manager_;
    std::unique_ptr<std::thread> router_thread_;
    std::unique_ptr<std::thread> manager_thread_;

    // For hosting an InterVehiclePortal
    goby::middleware::InterThreadTransporter interthread_;
    goby::zeromq::InterProcessPortal<goby::middleware::InterThreadTransporter> interprocess_;
    std::unique_ptr<goby::middleware::InterVehiclePortal<decltype(interprocess_)>> intervehicle_;
};

class DaemonConfigurator : public goby::middleware::ProtobufConfigurator<protobuf::GobyDaemonConfig>
{
  public:
    DaemonConfigurator(int argc, char* argv[])
        : goby::middleware::ProtobufConfigurator<protobuf::GobyDaemonConfig>(argc, argv)
    {
        protobuf::GobyDaemonConfig& cfg = mutable_cfg();

        cfg.mutable_interprocess()->set_client_name(cfg.app().name());

        // add ourself to the hold list if any are specified
        if (cfg.has_hold())
            cfg.mutable_hold()->add_required_client(cfg.app().name());

        if (cfg.has_intervehicle())
        {
            auto& intervehicle = *cfg.mutable_intervehicle();
            if (intervehicle.has_persist_subscriptions())
            {
                auto& p = *intervehicle.mutable_persist_subscriptions();
                if (!p.has_name())
                    p.set_name(cfg.interprocess().platform());
            }
        }
    }
};

} // namespace zeromq
} // namespace apps
} // namespace goby

int main(int argc, char* argv[])
{
    return goby::run<goby::apps::zeromq::Daemon>(
        goby::apps::zeromq::DaemonConfigurator(argc, argv));
}

goby::apps::zeromq::Daemon::Daemon()
    : router_context_(new zmq::context_t(app_cfg().router_threads())),
      manager_context_(new zmq::context_t(1)),
      router_(*router_context_, app_cfg().interprocess()),
      manager_(make_manager()),
      router_thread_(new std::thread([&] { router_.run(); })),
      manager_thread_(new std::thread([&] { manager_.run(); })),
      interprocess_(app_cfg().interprocess())
{
    if (!app_cfg().interprocess().has_platform())
    {
        glog.is(WARN) && glog << "Using default platform name of "
                              << app_cfg().interprocess().platform() << std::endl;
    }

    if (app_cfg().has_intervehicle())
        intervehicle_ =
            std::make_unique<goby::middleware::InterVehiclePortal<decltype(interprocess_)>>(
                interprocess_, app_cfg().intervehicle());

    // handle goby_terminate request
    interprocess_.subscribe<goby::middleware::groups::terminate_request,
                            goby::middleware::protobuf::TerminateRequest>(
        [this](const goby::middleware::protobuf::TerminateRequest& request) {
            bool match = false;
            goby::middleware::protobuf::TerminateResponse resp;
            std::tie(match, resp) =
                goby::middleware::terminate::check_terminate(request, app_cfg().app().name());
            if (match)
            {
                interprocess_.publish<goby::middleware::groups::terminate_response>(resp);
            }
        });

    // as gobyd mediates all interprocess() comms; wait until we get our result back from goby_terminate before shutting down
    interprocess_.subscribe<goby::middleware::groups::terminate_result>(
        [this](const goby::middleware::protobuf::TerminateResult& result) {
            std::cout << result.DebugString() << std::endl;

            auto our_pid = getpid();
            if (result.has_target_pid() &&
                static_cast<decltype(our_pid)>(result.target_pid()) == our_pid &&
                result.result() == goby::middleware::protobuf::TerminateResult::PROCESS_RESPONDED)
                quit();
        });

    interprocess_.ready();
}

goby::apps::zeromq::Daemon::~Daemon()
{
    manager_context_.reset();
    router_context_.reset();
    manager_thread_->join();
    router_thread_->join();
}

void goby::apps::zeromq::Daemon::run()
{
    if (intervehicle_)
    {
        intervehicle_->poll();

        //        intervehicle_->poll(std::chrono::milliseconds(100) / time::SimulatorSettings::warp_factor);
        //        goby::middleware::intervehicle::protobuf::Status status;
        //        status.set_tx_queue_size(intervehicle_->tx_queue_size());
        //        interprocess_.publish<goby::middleware::groups::intervehicle_outbound>(status);
    }
    else
    {
        interprocess_.poll();
    }
}
