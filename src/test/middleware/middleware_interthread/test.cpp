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

#include <atomic>
#include <deque>

#include "goby/middleware/transport.h"
#include "goby/util/debug_logger.h"
#include "test.pb.h"

// tests InterThreadTransporter

goby::InterThreadTransporter inproc1;
goby::InterThreadTransporter inproc2;

int publish_count = 0;
const int max_publish = 100;

std::atomic<int> ready(0);

extern constexpr goby::Group sample1{"Sample1"};
extern constexpr goby::Group sample2{"Sample2"};
extern constexpr goby::Group widget{"Widget"};

// thread 1
void publisher()
{
    double a = 0;
    while (publish_count < max_publish)
    {
        auto s1 = std::make_shared<Sample>();
        s1->set_a(a++);
        inproc1.publish<sample1>(s1);
        auto s2 = std::make_shared<Sample>();
        s2->set_a(s1->a() + 10);
        std::shared_ptr<const Sample> s2_const = s2;
        inproc1.publish<sample2>(s2_const);
        auto w1 = std::make_shared<Widget>();
        w1->set_b(s1->a() - 8);
        inproc1.publish<widget>(w1);
        ++publish_count;
    }
}

// thread 2

class Subscriber
{
  public:
    void run()
    {
        inproc2.subscribe<sample1, Sample>(
            [this](std::shared_ptr<const Sample> s) { handle_sample1(s); });
        inproc2.subscribe<sample2, Sample>(
            [this](std::shared_ptr<const Sample> s) { handle_sample2(s); });
        inproc2.subscribe<widget, Widget>(
            [this](std::shared_ptr<const Widget> w) { handle_widget1(w); });
        while (receive_count1 < max_publish || receive_count2 < max_publish ||
               receive_count3 < max_publish)
        {
            ++ready;
            inproc2.poll();
            //  std::cout << "Polled " << items  << " items. " << std::endl;
        }
    }

  private:
    void handle_sample1(std::shared_ptr<const Sample> sample)
    {
        std::thread::id this_id = std::this_thread::get_id();
        std::cout << this_id << ": Received1: " << sample->DebugString() << std::endl;
        assert(sample->a() == receive_count1);
        ++receive_count1;
    }
    void handle_sample2(std::shared_ptr<const Sample> sample)
    {
        std::thread::id this_id = std::this_thread::get_id();
        std::cout << this_id << ": Received2: " << sample->DebugString() << std::endl;
        assert(sample->a() == receive_count2 + 10);
        ++receive_count2;
    }

    void handle_widget1(std::shared_ptr<const Widget> widget)
    {
        std::thread::id this_id = std::this_thread::get_id();
        std::cout << this_id << ": Received3: " << widget->DebugString() << std::endl;
        assert(widget->b() == receive_count3 - 8);
        ++receive_count3;
    }

  private:
    int receive_count1 = {0};
    int receive_count2 = {0};
    int receive_count3 = {0};
};

int main(int argc, char* argv[])
{
    goby::glog.add_stream(goby::util::logger::DEBUG3, &std::cerr);
    goby::glog.set_name(argv[0]);
    goby::glog.set_lock_action(goby::util::logger_lock::lock);

    //    std::thread t3(subscriber);
    const int max_subs = 10;
    std::vector<Subscriber> subscribers(max_subs, Subscriber());
    std::vector<std::thread> threads;
    for (int i = 0; i < max_subs; ++i)
    { threads.push_back(std::thread(std::bind(&Subscriber::run, &subscribers.at(i)))); }

    while (ready < max_subs)
        usleep(1e5);

    std::thread t1(publisher);

    t1.join();

    for (int i = 0; i < max_subs; ++i) threads.at(i).join();

    std::cout << "all tests passed" << std::endl;
}
