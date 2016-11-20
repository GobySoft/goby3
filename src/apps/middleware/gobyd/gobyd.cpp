// Copyright 2009-2016 Toby Schneider (http://gobysoft.org/index.wt/people/toby)
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
#include "goby/middleware/transport-interprocess.h"

#include "config.pb.h"

using namespace goby::common::logger;

namespace goby
{
    class Daemon : public goby::common::ApplicationBase3
    {
    public:
        Daemon(goby::protobuf::GobyDaemonConfig* cfg);
        ~Daemon();
        
    private:
        void run() override;

    private:
        goby::protobuf::GobyDaemonConfig& cfg_;

        // for handling ZMQ Interprocess Communications
        std::unique_ptr<zmq::context_t> router_context_;
        std::unique_ptr<zmq::context_t> manager_context_;
        goby::ZMQRouter router_;
        goby::ZMQManager manager_;
        std::unique_ptr<std::thread> router_thread_;
        std::unique_ptr<std::thread> manager_thread_;
    };
}

int main(int argc, char* argv[])
{
    goby::protobuf::GobyDaemonConfig cfg;
    return goby::run<goby::Daemon>(argc, argv, &cfg);
}

goby::Daemon::Daemon(goby::protobuf::GobyDaemonConfig* cfg)
    : goby::common::ApplicationBase3(cfg),
      cfg_(*cfg),
      router_context_(new zmq::context_t(cfg_.router_threads())),
      manager_context_(new zmq::context_t(1)),
      router_(*router_context_, cfg_.interprocess_portal()),
      manager_(*manager_context_, cfg_.interprocess_portal(), router_),
      router_thread_(new std::thread([&] { router_.run(); })),
      manager_thread_(new std::thread([&] { manager_.run(); }))      
{
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
    sleep(1);
}
