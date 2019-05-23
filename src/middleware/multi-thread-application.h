// Copyright 2009-2018 Toby Schneider (http://gobysoft.org/index.wt/people/toby)
//                     GobySoft, LLC (2013-)
//                     Massachusetts Institute of Technology (2007-2014)
//                     Community contributors (see AUTHORS file)
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

#ifndef MULTITHREADAPPLICATION20170616H
#define MULTITHREADAPPLICATION20170616H

#include <boost/units/systems/si.hpp>

#include "goby/common/application.h"
#include "goby/common/exception.h"
#include "goby/middleware/thread.h"
#include "goby/middleware/transport-interprocess-zeromq.h"
#include "goby/middleware/transport-interthread.h"
#include "goby/middleware/transport-intervehicle.h"

#include "goby/middleware/terminate/terminate.h"

namespace goby
{
template <typename Config>
class SimpleThread
    : public Thread<Config, InterVehicleForwarder<InterProcessForwarder<InterThreadTransporter> > >
{
    using SimpleThreadBase =
        Thread<Config, InterVehicleForwarder<InterProcessForwarder<InterThreadTransporter> > >;

  public:
    SimpleThread(const Config& cfg, double loop_freq_hertz = 0, int index = -1)
        : SimpleThread(cfg, loop_freq_hertz * boost::units::si::hertz, index)
    {
    }

    SimpleThread(const Config& cfg, boost::units::quantity<boost::units::si::frequency> loop_freq,
                 int index = -1)
        : SimpleThreadBase(cfg, loop_freq, index)
    {
        interthread_.reset(new InterThreadTransporter);
        interprocess_.reset(new InterProcessForwarder<InterThreadTransporter>(*interthread_));
        intervehicle_.reset(
            new InterVehicleForwarder<InterProcessForwarder<InterThreadTransporter> >(
                *interprocess_));

        this->set_transporter(intervehicle_.get());

        interthread_->template subscribe<SimpleThreadBase::shutdown_group_, bool>(
            [this](const bool& shutdown) {
                if (shutdown)
                    SimpleThreadBase::thread_quit();
            });
    }

    InterVehicleForwarder<InterProcessForwarder<InterThreadTransporter> >& intervehicle()
    {
        return this->transporter();
    }

    InterProcessForwarder<InterThreadTransporter>& interprocess()
    {
        return this->transporter().inner();
    }

    InterThreadTransporter& interthread() { return this->transporter().inner().inner(); }

  private:
    std::unique_ptr<InterThreadTransporter> interthread_;
    std::unique_ptr<InterProcessForwarder<InterThreadTransporter> > interprocess_;
    std::unique_ptr<InterVehicleForwarder<InterProcessForwarder<InterThreadTransporter> > >
        intervehicle_;
};

template <class Config, class Transporter>
class MultiThreadApplicationBase : public goby::common::Application<Config>,
                                   public goby::Thread<Config, Transporter>
{
  private:
    struct ThreadManagement
    {
        std::atomic<bool> alive{true};
        std::unique_ptr<std::thread> thread;
    };

    static std::exception_ptr thread_exception_;

    std::map<std::type_index, std::map<int, ThreadManagement> > threads_;
    int running_thread_count_{0};

    goby::InterThreadTransporter interthread_;

  public:
    using MainThreadBase = goby::Thread<Config, Transporter>;

    MultiThreadApplicationBase(double loop_freq_hertz = 0)
        : MultiThreadApplicationBase(loop_freq_hertz * boost::units::si::hertz)
    {
    }

    MultiThreadApplicationBase(boost::units::quantity<boost::units::si::frequency> loop_freq,
                               Transporter* transporter)
        : goby::common::Application<Config>(),
          MainThreadBase(this->app_cfg(), transporter, loop_freq)
    {
        goby::glog.set_lock_action(goby::common::logger_lock::lock);

        interthread_
            .template subscribe<MainThreadBase::joinable_group_, std::pair<std::type_index, int> >(
                [this](const std::pair<std::type_index, int>& joinable) {
                    _join_thread(joinable.first, joinable.second);
                });
    }

    virtual ~MultiThreadApplicationBase() {}

    template <typename ThreadType> void launch_thread()
    {
        _launch_thread<ThreadType, Config, false>(-1, this->app_cfg());
    }
    template <typename ThreadType> void launch_thread(int index)
    {
        _launch_thread<ThreadType, Config, true>(index, this->app_cfg());
    }

    template <typename ThreadType, typename ThreadConfig>
    void launch_thread(const ThreadConfig& cfg)
    {
        _launch_thread<ThreadType, ThreadConfig, false>(-1, cfg);
    }
    template <typename ThreadType, typename ThreadConfig>
    void launch_thread(int index, const ThreadConfig& cfg)
    {
        _launch_thread<ThreadType, ThreadConfig, true>(index, cfg);
    }

    template <typename ThreadType> void join_thread(int index = -1)
    {
        auto type_i = std::type_index(typeid(ThreadType));
        _join_thread(type_i, index);
    }

    int running_thread_count() { return running_thread_count_; }

  protected:
    goby::InterThreadTransporter& interthread() { return interthread_; }
    virtual void finalize() override { join_all_threads(); }

    void join_all_threads()
    {
        if (running_thread_count_ > 0)
        {
            goby::glog.is(goby::common::logger::DEBUG1) &&
                goby::glog << "Requesting that all remaining threads shutdown cleanly..."
                           << std::endl;

            interthread_.publish<MainThreadBase::shutdown_group_>(true);

            // allow the threads to self-join
            while (running_thread_count_ > 0)
            {
                goby::glog.is(goby::common::logger::DEBUG1) &&
                    goby::glog << "Waiting for " << running_thread_count_ << " threads."
                               << std::endl;

                MainThreadBase::transporter().poll();
            }

            goby::glog.is(goby::common::logger::DEBUG1) &&
                goby::glog << "All threads cleanly joined." << std::endl;
        }
    }

  private:
    void run() override
    {
        try
        {
            MainThreadBase::run_once();
        }
        catch (std::exception& e)
        {
            goby::glog.is(goby::common::logger::WARN) &&
                goby::glog << "MultiThreadApplicationBase:: uncaught exception: " << e.what()
                           << std::endl;
            throw;
        }
    }

    template <typename ThreadType, typename ThreadConfig, bool has_index>
    void _launch_thread(int index, const ThreadConfig& cfg);

    void _join_thread(const std::type_index& type_i, int index);
};

template <class Config>
class MultiThreadApplication
    : public MultiThreadApplicationBase<
          Config, InterVehicleForwarder<InterProcessPortal<InterThreadTransporter> > >
{
  private:
    InterProcessPortal<InterThreadTransporter> interprocess_;
    InterVehicleForwarder<InterProcessPortal<InterThreadTransporter> > intervehicle_;
    using Base = MultiThreadApplicationBase<
        Config, InterVehicleForwarder<InterProcessPortal<InterThreadTransporter> > >;

  public:
    MultiThreadApplication(double loop_freq_hertz = 0)
        : MultiThreadApplication(loop_freq_hertz * boost::units::si::hertz)
    {
    }

    MultiThreadApplication(boost::units::quantity<boost::units::si::frequency> loop_freq)
        : Base(loop_freq, &intervehicle_),
          interprocess_(Base::interthread(), this->app_cfg().interprocess()),
          intervehicle_(interprocess_)
    {
        // handle goby_terminate request
        this->interprocess()
            .template subscribe<groups::terminate_request, protobuf::TerminateRequest>(
                [this](const protobuf::TerminateRequest& request) {
                    bool match = false;
                    protobuf::TerminateResponse resp;
                    std::tie(match, resp) =
                        goby::terminate::check_terminate(request, this->app_cfg().app().name());
                    if (match)
                    {
                        this->interprocess().template publish<groups::terminate_response>(resp);
                        this->quit();
                    }
                });
    }

    virtual ~MultiThreadApplication() {}

  protected:
    InterThreadTransporter& interthread() { return interprocess_.inner(); }
    InterProcessPortal<InterThreadTransporter>& interprocess() { return interprocess_; }
    InterVehicleForwarder<InterProcessPortal<InterThreadTransporter> >& intervehicle()
    {
        return intervehicle_;
    }
};

template <class Config>
class MultiThreadStandaloneApplication
    : public MultiThreadApplicationBase<Config, InterThreadTransporter>
{
  private:
    using Base = MultiThreadApplicationBase<Config, InterThreadTransporter>;

  public:
    MultiThreadStandaloneApplication(double loop_freq_hertz = 0)
        : MultiThreadStandaloneApplication(loop_freq_hertz * boost::units::si::hertz)
    {
    }

    MultiThreadStandaloneApplication(boost::units::quantity<boost::units::si::frequency> loop_freq)
        : Base(loop_freq, &Base::interthread())
    {
    }
    virtual ~MultiThreadStandaloneApplication() {}

  protected:
};

template <class Config> class MultiThreadTest : public MultiThreadStandaloneApplication<Config>
{
  private:
    using Base = MultiThreadStandaloneApplication<Config>;

  public:
    MultiThreadTest(
        boost::units::quantity<boost::units::si::frequency> loop_freq = 0 * boost::units::si::hertz)
        : Base(loop_freq)
    {
    }
    virtual ~MultiThreadTest() {}

  protected:
    // so we can add on threads that publish to the outside for testing
    InterThreadTransporter& interprocess() { return Base::interthread(); }
    InterThreadTransporter& intervehicle() { return Base::interthread(); }
};

// selects which constructor to use based on whether the thread is launched with an index or not
template <typename ThreadType, typename ThreadConfig, bool has_index> struct ThreadTypeSelector
{
};
template <typename ThreadType, typename ThreadConfig>
struct ThreadTypeSelector<ThreadType, ThreadConfig, false>
{
    static std::shared_ptr<ThreadType> thread(const ThreadConfig& cfg, int index = -1)
    {
        return std::make_shared<ThreadType>(cfg);
    };
};
template <typename ThreadType, typename ThreadConfig>
struct ThreadTypeSelector<ThreadType, ThreadConfig, true>
{
    static std::shared_ptr<ThreadType> thread(const ThreadConfig& cfg, int index)
    {
        return std::make_shared<ThreadType>(cfg, index);
    };
};
} // namespace goby

template <class Config, class Transporter>
std::exception_ptr
    goby::MultiThreadApplicationBase<Config, Transporter>::thread_exception_(nullptr);

template <class Config, class Transporter>
template <typename ThreadType, typename ThreadConfig, bool has_index>
void goby::MultiThreadApplicationBase<Config, Transporter>::_launch_thread(int index,
                                                                           const ThreadConfig& cfg)
{
    std::type_index type_i = std::type_index(typeid(ThreadType));

    if (threads_[type_i].count(index) && threads_[type_i][index].alive)
        throw(Exception(std::string("Thread of type: ") + type_i.name() + " and index " +
                        std::to_string(index) + " is already launched and running."));

    auto& thread_manager = threads_[type_i][index];
    thread_manager.alive = true;

    // copy configuration
    auto thread_lambda = [this, type_i, index, cfg, &thread_manager]() {
        std::shared_ptr<ThreadType> goby_thread(
            ThreadTypeSelector<ThreadType, ThreadConfig, has_index>::thread(cfg, index));

        try
        {
            goby_thread->run(thread_manager.alive);
        }
        catch (...)
        {
            thread_exception_ = std::current_exception();
        }

        interthread_.publish<MainThreadBase::joinable_group_>(std::make_pair(type_i, index));
    };

    thread_manager.thread = std::unique_ptr<std::thread>(new std::thread(thread_lambda));
    ++running_thread_count_;
}

template <class Config, class Transporter>
void goby::MultiThreadApplicationBase<Config, Transporter>::_join_thread(
    const std::type_index& type_i, int index)
{
    if (!threads_.count(type_i) || !threads_[type_i].count(index))
        throw(Exception(std::string("No thread of type: ") + type_i.name() + " and index " +
                        std::to_string(index) + " to join."));

    if (threads_[type_i][index].thread)
    {
        goby::glog.is(goby::common::logger::DEBUG1) &&
            goby::glog << "Joining thread: " << type_i.name() << " index " << index << std::endl;

        threads_[type_i][index].alive = false;
        threads_[type_i][index].thread->join();
        threads_[type_i][index].thread.reset();
        --running_thread_count_;

        if (thread_exception_)
        {
            goby::glog.is(goby::common::logger::WARN) &&
                goby::glog << "Thread type: " << type_i.name() << ", index: " << index
                           << " had an uncaught exception" << std::endl;
            std::rethrow_exception(thread_exception_);
        }
    }
    else
    {
        goby::glog.is(goby::common::logger::DEBUG1) &&
            goby::glog << "Already joined thread: " << type_i.name() << " index " << index
                       << std::endl;
    }
}

#endif
