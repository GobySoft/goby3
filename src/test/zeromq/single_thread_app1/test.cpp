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

#include "goby/middleware/marshalling/protobuf.h"
#include "goby/time.h"
#include "goby/time/io.h"
#include "goby/zeromq/application/single_thread.h"

#include <boost/units/io.hpp>
#include <sys/types.h>
#include <sys/wait.h>

#include "test.pb.h"
using goby::glog;
using namespace goby::util::logger;
using namespace goby::test::zeromq::protobuf;

extern constexpr goby::middleware::Group widget1{"Widget1"};

using Base = goby::zeromq::SingleThreadApplication<TestConfig>;

const std::string platform_name{"single_thread_app1"};

namespace goby
{
namespace test
{
namespace zeromq
{
class TestConfigurator : public goby::middleware::ProtobufConfigurator<TestConfig>
{
  public:
    TestConfigurator(int argc, char* argv[])
        : goby::middleware::ProtobufConfigurator<TestConfig>(argc, argv)
    {
        TestConfig& cfg = mutable_cfg();
        cfg.mutable_interprocess()->set_platform(platform_name);
    }
};

class TestApp : public Base
{
  public:
    TestApp() : Base(10)
    {
        interprocess().subscribe<widget1, Widget>([this](const Widget& w) { post(w); });
    }

    void loop() override
    {
        static int i = 0;
        ++i;
        // wait for zeromq pub/sub setup
        if (i < 1 * loop_frequency_hertz())
        {
            return;
        }
        else if (i > (10 + 1 * loop_frequency_hertz()))
        {
            quit();
        }
        else
        {
            assert(rx_count_ == tx_count_);
            std::cout << goby::time::SystemClock::now() << std::endl;
            Widget w;
            w.set_b(++tx_count_);
            std::cout << "Tx: " << w.DebugString() << std::flush;
            interprocess().publish<widget1>(w);
        }
    }

    void post(const Widget& widget)
    {
        std::cout << "Rx: " << widget.DebugString() << std::flush;
        assert(widget.b() == tx_count_);
        ++rx_count_;
    }

  private:
    int tx_count_{0};
    int rx_count_{0};
};
} // namespace zeromq
} // namespace test
} // namespace goby

int main(int argc, char* argv[])
{
    int child_pid = fork();

    std::unique_ptr<std::thread> t2, t3;
    std::unique_ptr<zmq::context_t> manager_context;
    std::unique_ptr<zmq::context_t> router_context;

    if (child_pid != 0)
    {
        goby::zeromq::protobuf::InterProcessPortalConfig cfg;
        cfg.set_platform(platform_name);
        manager_context.reset(new zmq::context_t(1));
        router_context.reset(new zmq::context_t(1));
        goby::zeromq::Router router(*router_context, cfg);
        t2.reset(new std::thread([&] { router.run(); }));
        goby::zeromq::Manager manager(*manager_context, cfg, router);
        t3.reset(new std::thread([&] { manager.run(); }));
        int wstatus;
        wait(&wstatus);
        router_context.reset();
        manager_context.reset();
        t2->join();
        t3->join();
        if (wstatus != 0)
            exit(EXIT_FAILURE);
    }
    else
    {
        // wait for router to set up
        sleep(1);
        return goby::run<goby::test::zeromq::TestApp>(
            goby::test::zeromq::TestConfigurator(argc, argv));
    }
}
