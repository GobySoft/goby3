// Copyright 2013-2022:
//   GobySoft, LLC (2013-)
//   Massachusetts Institute of Technology (2007-2014)
//   Community contributors (see AUTHORS file)
// File authors:
//   Toby Schneider <toby@gobysoft.org>
//
//
// This file is part of the Goby Underwater Autonomy Project Libraries
// ("The Goby Libraries").
//
// The Goby Libraries are free software: you can redistribute them and/or modify
// them under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 2.1 of the License, or
// (at your option) any later version.
//
// The Goby Libraries are distributed in the hope that they will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with Goby.  If not, see <http://www.gnu.org/licenses/>.

#ifndef GOBY_ZEROMQ_LIAISON_LIAISON_CONTAINER_H
#define GOBY_ZEROMQ_LIAISON_LIAISON_CONTAINER_H

#include <queue>

#include <Wt/WColor.h>
#include <Wt/WContainerWidget.h>
#include <Wt/WText.h>
#include <Wt/WTimer.h>

#include "goby/middleware/marshalling/protobuf.h"

#include "goby/middleware/group.h"
#include "goby/zeromq/application/multi_thread.h"
#include "goby/zeromq/protobuf/liaison_config.pb.h"

namespace goby
{
namespace zeromq
{
const Wt::WColor goby_blue(28, 159, 203);
const Wt::WColor goby_orange(227, 96, 52);

inline std::string liaison_internal_publish_socket_name()
{
    return "liaison_internal_publish_socket";
}
inline std::string liaison_internal_subscribe_socket_name()
{
    return "liaison_internal_subscribe_socket";
}

class LiaisonContainer : public Wt::WContainerWidget
{
  public:
    LiaisonContainer()
    {
        setStyleClass("fill");
        /* addWidget(new Wt::WText("<hr/>")); */
        /* addWidget(name_); */
        /* addWidget(new Wt::WText("<hr/>")); */
    }

    virtual ~LiaisonContainer()
    {
        goby::glog.is_debug2() && goby::glog << "~LiaisonContainer(): " << name() << std::endl;
    }

    void set_name(const Wt::WString& name) { name_.setText(name); }

    const Wt::WString& name() { return name_.text(); }

    virtual void focus() {}
    virtual void unfocus() {}
    virtual void cleanup() {}

  private:
    Wt::WText name_;
};

template <typename Derived, typename GobyThread>
class LiaisonContainerWithComms : public LiaisonContainer
{
  public:
    LiaisonContainerWithComms(const goby::apps::zeromq::protobuf::LiaisonConfig& cfg)
    {
        static std::atomic<int> index(0);
        index_ = index++;

        // copy configuration
        auto thread_lambda = [this, cfg]() {
            {
                std::lock_guard<std::mutex> l(goby_thread_mutex);
                goby_thread_ =
                    std::make_unique<GobyThread>(static_cast<Derived*>(this), cfg, index_);
            }

            try
            {
                goby_thread_->run(thread_alive_);
            }
            catch (...)
            {
                thread_exception_ = std::current_exception();
            }

            {
                std::lock_guard<std::mutex> l(goby_thread_mutex);
                goby_thread_.reset();
            }
        };

        thread_ = std::unique_ptr<std::thread>(new std::thread(thread_lambda));

        // wait for thread to be created
        while (goby_thread() == nullptr) usleep(1000);

        comms_timer_.setInterval(1 / cfg.update_freq() * 1.0e3);
        comms_timer_.timeout().connect(
            [this](const Wt::WMouseEvent&) { this->process_from_comms(); });
        comms_timer_.start();
    }

    virtual ~LiaisonContainerWithComms()
    {
        thread_alive_ = false;
        thread_->join();

        if (thread_exception_)
        {
            goby::glog.is_warn() && goby::glog << "Comms thread had an uncaught exception"
                                               << std::endl;
            std::rethrow_exception(thread_exception_);
        }

        goby::glog.is_debug2() && goby::glog << "~LiaisonContainerWithComms(): " << name()
                                             << std::endl;
    }

    void post_to_wt(std::function<void()> func)
    {
        std::lock_guard<std::mutex> l(comms_to_wt_mutex);
        comms_to_wt_queue.push(func);
    }

    void process_from_wt()
    {
        std::lock_guard<std::mutex> l(wt_to_comms_mutex);
        while (!wt_to_comms_queue.empty())
        {
            wt_to_comms_queue.front()();
            wt_to_comms_queue.pop();
        }
    }

  protected:
    GobyThread* goby_thread()
    {
        std::lock_guard<std::mutex> l(goby_thread_mutex);
        return goby_thread_.get();
    }

    void post_to_comms(std::function<void()> func)
    {
        std::lock_guard<std::mutex> l(wt_to_comms_mutex);
        wt_to_comms_queue.push(func);
    }

    void process_from_comms()
    {
        std::lock_guard<std::mutex> l(comms_to_wt_mutex);
        while (!comms_to_wt_queue.empty())
        {
            comms_to_wt_queue.front()();
            comms_to_wt_queue.pop();
        }
    }

    void update_comms_freq(double hertz)
    {
        comms_timer_.stop();
        comms_timer_.setInterval(1 / hertz * 1.0e3);
        comms_timer_.start();
    }

  private:
    // for comms
    std::mutex comms_to_wt_mutex;
    std::queue<std::function<void()>> comms_to_wt_queue;
    std::mutex wt_to_comms_mutex;
    std::queue<std::function<void()>> wt_to_comms_queue;

    // only protects the unique_ptr, not the underlying thread
    std::mutex goby_thread_mutex;
    std::unique_ptr<GobyThread> goby_thread_{nullptr};

    int index_;
    std::unique_ptr<std::thread> thread_;
    std::atomic<bool> thread_alive_{true};
    std::exception_ptr thread_exception_;

    Wt::WTimer comms_timer_;
};

template <typename WtContainer>
class LiaisonCommsThread
    : public goby::middleware::SimpleThread<goby::apps::zeromq::protobuf::LiaisonConfig>
{
  public:
    LiaisonCommsThread(WtContainer* container,
                       const goby::apps::zeromq::protobuf::LiaisonConfig& config, int index)
        : goby::middleware::SimpleThread<goby::apps::zeromq::protobuf::LiaisonConfig>(
              config, config.update_freq() * boost::units::si::hertz, index),
          container_(container)
    {
    }

    void loop() override
    {
        //        goby::glog.is_debug3() && goby::glog << "LiaisonCommsThread " << this->index() << " loop()"
        //                                             << std::endl;
        container_->process_from_wt();
    }

  private:
    WtContainer* container_;
};
} // namespace zeromq
} // namespace goby
#endif
