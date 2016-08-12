#include <deque>
#include <atomic>

#include "goby/common/logger.h"
#include "goby/sandbox/transport.h"
#include "test.pb.h"

// tests InterThreadTransporter

goby::InterThreadTransporter inproc;

int publish_count = 0;
const int max_publish = 10000;

std::atomic<int> ready(0);

// thread 1
void publisher()
{
    double a = 0;
    while(publish_count < max_publish)
    {
        auto s1 = std::make_shared<Sample>();
        s1->set_a(a++);
        inproc.publish(s1, "Sample1");
        auto s2 = std::make_shared<Sample>();
        s2->set_a(s1->a() + 10);
        inproc.publish(s2, "Sample2");
        auto w1 = std::make_shared<Widget>();
        w1->set_b(s1->a() - 8);
        inproc.publish(w1, "Widget");
        ++publish_count;
    }    
}

// thread 2

class Subscriber
{
public:
    void run()
        {
            inproc.subscribe<Sample>([this](std::shared_ptr<const Sample> s) { handle_sample1(s); }, "Sample1");
            inproc.subscribe<Sample>([this](std::shared_ptr<const Sample> s) { handle_sample2(s); }, "Sample2");
            inproc.subscribe<Widget>([this](std::shared_ptr<const Widget> w) { handle_widget1(w); }, "Widget");
            while(receive_count1 < max_publish || receive_count2 < max_publish || receive_count3 < max_publish)
            {
                ++ready;
                int items = inproc.poll();
                //  std::cout << "Polled " << items  << " items. " << std::endl;
            }
        }
private:
    void handle_sample1(std::shared_ptr<const Sample> sample)
        {
            //            std::thread::id this_id = std::this_thread::get_id();
            // std::cout << this_id << ": Received1: " << sample->DebugString() << std::endl;
            assert(sample->a() == receive_count1);
            ++receive_count1;
        }
    void handle_sample2(std::shared_ptr<const Sample> sample)
        {
            //std::thread::id this_id = std::this_thread::get_id();
            // std::cout << this_id << ": Received2: " << sample->DebugString() << std::endl;
            assert(sample->a() == receive_count2+10);
            ++receive_count2;
        }

    void handle_widget1(std::shared_ptr<const Widget> widget)
        {
            //std::thread::id this_id = std::this_thread::get_id();
            // std::cout << this_id << ": Received3: " << widget->DebugString() << std::endl;
            assert(widget->b() == receive_count3-8);
            ++receive_count3;
        }

private:
    int receive_count1 = {0};
    int receive_count2 = {0};
    int receive_count3 = {0};
};



int main(int argc, char* argv[])
{
    goby::glog.add_stream(goby::common::logger::DEBUG3, &std::cerr);
    goby::glog.set_name(argv[0]);
    goby::glog.set_lock_action(goby::common::logger_lock::lock);
    
    //    std::thread t3(subscriber);
    const int max_subs = 10;
    std::vector<Subscriber> subscribers(max_subs, Subscriber());
    std::vector<std::thread> threads;
    for(int i = 0; i < max_subs; ++i)
    {
        threads.push_back(std::thread(std::bind(&Subscriber::run, &subscribers.at(i))));
    }

    while(ready < max_subs)
        usleep(1e5);
    
    std::thread t1(publisher);
 
    t1.join();

    for(int i = 0; i < max_subs; ++i)
        threads.at(i).join();
    
    std::cout << "all tests passed" << std::endl;
}
