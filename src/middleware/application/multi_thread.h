// Copyright 2019-2020:
//   GobySoft, LLC (2013-)
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

#ifndef MULTITHREADAPPLICATION20170616H
#define MULTITHREADAPPLICATION20170616H

#include <boost/units/systems/si.hpp>

#include "goby/exception.h"
#include "goby/middleware/application/detail/thread_type_selector.h"
#include "goby/middleware/application/interface.h"
#include "goby/middleware/application/thread.h"

#include "goby/middleware/transport/interprocess.h"
#include "goby/middleware/transport/interthread.h"
#include "goby/middleware/transport/intervehicle.h"

#include "goby/middleware/coroner/coroner.h"
#include "goby/middleware/terminate/terminate.h"

namespace goby
{
namespace middleware
{
/// \brief Implements Thread for a three layer middleware setup ([ intervehicle [ interprocess [ interthread ] ] ]) based around InterVehicleForwarder.
///
/// \tparam Config Configuration type
/// Derive from this class to create standalone threads that can be launched and joined by MultiThreadApplication's launch_thread and join_thread methods.
template <typename Config>
class SimpleThread
    : public Thread<Config, InterVehicleForwarder<InterProcessForwarder<InterThreadTransporter>>>
{
    using SimpleThreadBase =
        Thread<Config, InterVehicleForwarder<InterProcessForwarder<InterThreadTransporter>>>;

  public:
    /// \brief Construct a thread with a given configuration, optionally a loop frequency and/or index
    ///
    /// \param cfg Data to configure the code running in this thread
    /// \param loop_freq_hertz The frequency at which to attempt to call loop(), assuming the thread isn't blocked handling transporter callbacks (e.g. subscribe callbacks). Zero or negative indicates loop() will never be called.
    /// \param index Numeric index to identify this instantiation of the SimpleThread (only necessary if multiple Threads of the same type are created)
    SimpleThread(const Config& cfg, double loop_freq_hertz = 0, int index = -1)
        : SimpleThread(cfg, loop_freq_hertz * boost::units::si::hertz, index)
    {
    }

    /// \brief Construct a thread with a given configuration, a loop frequency (using boost::units) and optionally an index
    ///
    /// \param cfg Data to configure the code running in this thread
    /// \param loop_freq The frequency at which to attempt to call loop(), assuming the thread isn't blocked handling transporter callbacks (e.g. subscribe callbacks). Zero or negative indicates loop() will never be called.
    /// \param index Numeric index to identify this instantiation of the SimpleThread (only necessary if multiple Threads of the same type are created)
    SimpleThread(const Config& cfg, boost::units::quantity<boost::units::si::frequency> loop_freq,
                 int index = -1)
        : SimpleThreadBase(cfg, loop_freq, index)
    {
        interthread_.reset(new InterThreadTransporter);
        interprocess_.reset(new InterProcessForwarder<InterThreadTransporter>(*interthread_));
        intervehicle_.reset(
            new InterVehicleForwarder<InterProcessForwarder<InterThreadTransporter>>(
                *interprocess_));

        this->set_transporter(intervehicle_.get());
    }

    /// \brief Access the transporter on the intervehicle layer (which wraps interprocess and interthread)
    InterVehicleForwarder<InterProcessForwarder<InterThreadTransporter>>& intervehicle()
    {
        return this->transporter();
    }

    /// \brief Access the transporter on the interprocess layer (which wraps interthread)
    InterProcessForwarder<InterThreadTransporter>& interprocess()
    {
        return this->transporter().inner();
    }

    /// \brief Access the transporter on the interthread layer (this is the innermost transporter)
    InterThreadTransporter& interthread() { return this->transporter().innermost(); }

  private:
    std::unique_ptr<InterThreadTransporter> interthread_;
    std::unique_ptr<InterProcessForwarder<InterThreadTransporter>> interprocess_;
    std::unique_ptr<InterVehicleForwarder<InterProcessForwarder<InterThreadTransporter>>>
        intervehicle_;
};

/// \brief Thread that simply publishes an empty message on its loop interval to TimerThread::group
///
/// This can be launched to provide a simple timer by subscribing to TimerThread::group.
///
/// For example, to create timer that expires every two seconds:
/// ```
/// launch_thread<goby::middleware::TimerThread<0>>(0.5*boost::units::si::hertz);
/// interthread().subscribe_empty<goby::middleware::TimerThread<0>::group>([]() { std::cout << "Timer expired." << std::endl; });
/// ```
template <int i>
class TimerThread
    : public Thread<boost::units::quantity<boost::units::si::frequency>, InterThreadTransporter>
{
    using ThreadBase =
        Thread<boost::units::quantity<boost::units::si::frequency>, InterThreadTransporter>;

  public:
    static constexpr goby::middleware::Group expire_group{"goby::middleware::TimerThread::timer",
                                                          i};

    TimerThread(const boost::units::quantity<boost::units::si::frequency>& freq)
        : ThreadBase(freq, &interthread_, freq)
    {
    }

  private:
    void loop() override { interthread_.template publish_empty<expire_group>(); }

  private:
    InterThreadTransporter interthread_;
};

template <int i> const goby::middleware::Group TimerThread<i>::expire_group;

/// \brief Base class for creating multiple thread applications
///
/// \tparam Config Configuration type
/// \tparam Transporter Transporter type
template <class Config, class Transporter>
class MultiThreadApplicationBase : public goby::middleware::Application<Config>,
                                   public goby::middleware::Thread<Config, Transporter>
{
  private:
    struct ThreadManagement
    {
        ThreadManagement() = default;
        ~ThreadManagement()
        {
            if (thread)
            {
                goby::glog.is(goby::util::logger::DEBUG1) &&
                    goby::glog << "Joining thread: " << name << std::endl;
                alive = false;
                thread->join();
            }
        }

        std::atomic<bool> alive{true};
        std::string name;
        std::unique_ptr<std::thread> thread;
    };

    static std::exception_ptr thread_exception_;

    std::map<std::type_index, std::map<int, ThreadManagement>> threads_;
    int running_thread_count_{0};
    InterThreadTransporter interthread_;

  public:
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
        // request thread self-join
        auto type_i = std::type_index(typeid(ThreadType));
        ThreadIdentifier ti{type_i, index};
        interthread_.publish<MainThreadBase::shutdown_group_>(ti);
    }

    template <int i>
    void launch_timer(boost::units::quantity<boost::units::si::frequency> freq,
                      std::function<void()> on_expire)
    {
        launch_thread<goby::middleware::TimerThread<i>>(freq);
        this->interthread()
            .template subscribe_empty<goby::middleware::TimerThread<i>::expire_group>(on_expire);
    }

    template <int i> void join_timer() { join_thread<goby::middleware::TimerThread<i>>(); }

    int running_thread_count() { return running_thread_count_; }

  protected:
    using MainThreadBase = Thread<Config, Transporter>;

    MultiThreadApplicationBase(boost::units::quantity<boost::units::si::frequency> loop_freq,
                               Transporter* transporter)
        : goby::middleware::Application<Config>(),
          MainThreadBase(this->app_cfg(), transporter, loop_freq)
    {
        goby::glog.set_lock_action(goby::util::logger_lock::lock);

        interthread_.template subscribe<MainThreadBase::joinable_group_>(
            [this](const ThreadIdentifier& joinable) {
                _join_thread(joinable.type_i, joinable.index);
            });
    }

    virtual ~MultiThreadApplicationBase() {}

    InterThreadTransporter& interthread() { return interthread_; }
    virtual void finalize() override { join_all_threads(); }

    void join_all_threads()
    {
        if (running_thread_count_ > 0)
        {
            goby::glog.is(goby::util::logger::DEBUG1) &&
                goby::glog << "Requesting that all remaining threads shutdown cleanly..."
                           << std::endl;

            ThreadIdentifier ti;
            ti.all_threads = true;
            interthread_.publish<MainThreadBase::shutdown_group_>(ti);

            // allow the threads to self-join
            while (running_thread_count_ > 0)
            {
                goby::glog.is(goby::util::logger::DEBUG1) && goby::glog << "Waiting for "
                                                                        << running_thread_count_
                                                                        << " threads." << std::endl;

                MainThreadBase::transporter().poll();
            }

            goby::glog.is(goby::util::logger::DEBUG1) && goby::glog << "All threads cleanly joined."
                                                                    << std::endl;
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
            goby::glog.is(goby::util::logger::WARN) &&
                goby::glog << "MultiThreadApplicationBase:: uncaught exception: " << e.what()
                           << std::endl;
            std::terminate();
        }
    }

    template <typename ThreadType, typename ThreadConfig, bool has_index>
    void _launch_thread(int index, const ThreadConfig& cfg);

    void _join_thread(const std::type_index& type_i, int index);
};

/// \brief Base class for building multithreaded applications for a given implementation of the InterProcessPortal. This class isn't used directly by user applications, for that use a specific implementation, e.g. zeromq::MultiThreadApplication
///
/// \tparam Config Configuration type
/// \tparam InterProcessPortal the interprocess portal type to use (e.g. zeromq::InterProcessPortal).
template <class Config, template <class InnerTransporter> class InterProcessPortal>
class MultiThreadApplication
    : public MultiThreadApplicationBase<
          Config, InterVehicleForwarder<InterProcessPortal<InterThreadTransporter>>>
{
  private:
    InterProcessPortal<InterThreadTransporter> interprocess_;
    InterVehicleForwarder<InterProcessPortal<InterThreadTransporter>> intervehicle_;
    using Base = MultiThreadApplicationBase<
        Config, InterVehicleForwarder<InterProcessPortal<InterThreadTransporter>>>;

  public:
    /// \brief Construct the application calling loop() at the given frequency (double overload)
    ///
    /// \param loop_freq_hertz The frequency at which to attempt to call loop(), assuming the main thread isn't blocked handling transporter callbacks (e.g. subscribe callbacks). Zero or negative indicates loop() will never be called.
    MultiThreadApplication(double loop_freq_hertz = 0)
        : MultiThreadApplication(loop_freq_hertz * boost::units::si::hertz)
    {
    }

    /// \brief Construct the application calling loop() at the given frequency (boost::units overload)
    ///
    /// \param loop_freq The frequency at which to attempt to call loop(), assuming the main thread isn't blocked handling transporter callbacks (e.g. subscribe callbacks). Zero or negative indicates loop() will never be called.
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
                        terminate::check_terminate(request, this->app_cfg().app().name());
                    if (match)
                    {
                        this->interprocess().template publish<groups::terminate_response>(resp);
                        this->quit();
                    }
                });

        // handle goby_coroner request
        this->interprocess().template subscribe<groups::health_request, protobuf::HealthRequest>(
            [this](const protobuf::HealthRequest& request) {
                protobuf::ProcessHealth resp;
                resp.set_name(this->app_name());
                resp.set_pid(getpid());
                this->thread_health(*resp.mutable_main());
                this->interprocess().template publish<groups::health_response>(resp);
            });
    }

    virtual ~MultiThreadApplication() {}

  protected:
    InterThreadTransporter& interthread() { return interprocess_.inner(); }
    InterProcessPortal<InterThreadTransporter>& interprocess() { return interprocess_; }
    InterVehicleForwarder<InterProcessPortal<InterThreadTransporter>>& intervehicle()
    {
        return intervehicle_;
    }

    virtual void health(goby::middleware::protobuf::ThreadHealth& health) override
    {
        health.set_name(this->app_name());
        health.set_state(goby::middleware::protobuf::HEALTH__OK);
    }
};

/// \brief Base class for building multithreaded Goby applications that do not have perform any interprocess (or outer) communications, but only communicate internally via the InterThreadTransporter
///
/// \tparam Config Configuration type
template <class Config>
class MultiThreadStandaloneApplication
    : public MultiThreadApplicationBase<Config, InterThreadTransporter>
{
  private:
    using Base = MultiThreadApplicationBase<Config, InterThreadTransporter>;

  public:
    /// \brief Construct the application calling loop() at the given frequency (double overload)
    ///
    /// \param loop_freq_hertz The frequency at which to attempt to call loop(), assuming the main thread isn't blocked handling transporter callbacks (e.g. subscribe callbacks). Zero or negative indicates loop() will never be called.
    MultiThreadStandaloneApplication(double loop_freq_hertz = 0)
        : MultiThreadStandaloneApplication(loop_freq_hertz * boost::units::si::hertz)
    {
    }

    /// \brief Construct the application calling loop() at the given frequency (boost::units overload)
    ///
    /// \param loop_freq The frequency at which to attempt to call loop(), assuming the main thread isn't blocked handling transporter callbacks (e.g. subscribe callbacks). Zero or negative indicates loop() will never be called.
    MultiThreadStandaloneApplication(boost::units::quantity<boost::units::si::frequency> loop_freq)
        : Base(loop_freq, &Base::interthread())
    {
    }
    virtual ~MultiThreadStandaloneApplication() {}

  protected:
};

/// \brief Base class for building multithreaded Goby tests that do not have perform any interprocess (or outer) communications, but only communicate internally via the InterThreadTransporter. The only difference with this class and MultiThreadStandaloneApplication is that the interprocess() and intervehicle() methods are implemented here (as dummy calls to interthread()) to allow this to be a drop-in replacement for testing interthread comms on existing MultiThreadApplication subclasses.
///
/// \tparam Config Configuration type
template <class Config> class MultiThreadTest : public MultiThreadStandaloneApplication<Config>
{
  private:
    using Base = MultiThreadStandaloneApplication<Config>;

  public:
    /// \brief Construct the test running at the given frequency
    ///
    /// \param loop_freq The frequency at which to attempt to call loop(), assuming the main thread isn't blocked handling transporter callbacks (e.g. subscribe callbacks). Zero or negative indicates loop() will never be called.
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

} // namespace middleware

template <class Config, class Transporter>
std::exception_ptr
    goby::middleware::MultiThreadApplicationBase<Config, Transporter>::thread_exception_(nullptr);

template <class Config, class Transporter>
template <typename ThreadType, typename ThreadConfig, bool has_index>
void goby::middleware::MultiThreadApplicationBase<Config, Transporter>::_launch_thread(
    int index, const ThreadConfig& cfg)
{
    std::type_index type_i = std::type_index(typeid(ThreadType));

    if (threads_[type_i].count(index) && threads_[type_i][index].alive)
        throw(Exception(std::string("Thread of type: ") + type_i.name() + " and index " +
                        std::to_string(index) + " is already launched and running."));

    auto& thread_manager = threads_[type_i][index];
    thread_manager.alive = true;

    // copy configuration
    auto thread_lambda = [this, type_i, index, cfg, &thread_manager]() {
        try
        {
            std::shared_ptr<ThreadType> goby_thread(
                detail::ThreadTypeSelector<ThreadType, ThreadConfig, has_index>::thread(cfg,
                                                                                        index));
            goby_thread->set_type_index(type_i);
            goby_thread->run(thread_manager.alive);
        }
        catch (...)
        {
            thread_exception_ = std::current_exception();
        }

        interthread_.publish<MainThreadBase::joinable_group_>(ThreadIdentifier{type_i, index});
    };

    thread_manager.name = std::string(typeid(ThreadType).name()) + "_index" + std::to_string(index);
    thread_manager.thread = std::unique_ptr<std::thread>(new std::thread(thread_lambda));
    ++running_thread_count_;
}

template <class Config, class Transporter>
void goby::middleware::MultiThreadApplicationBase<Config, Transporter>::_join_thread(
    const std::type_index& type_i, int index)
{
    if (!threads_.count(type_i) || !threads_[type_i].count(index))
        throw(Exception(std::string("No thread of type: ") + type_i.name() + " and index " +
                        std::to_string(index) + " to join."));

    if (threads_[type_i][index].thread)
    {
        goby::glog.is(goby::util::logger::DEBUG1) &&
            goby::glog << "Joining thread: " << type_i.name() << " index " << index << std::endl;

        threads_[type_i][index].alive = false;
        threads_[type_i][index].thread->join();
        threads_[type_i][index].thread.reset();
        --running_thread_count_;

        goby::glog.is(goby::util::logger::DEBUG1) &&
            goby::glog << "Joined thread: " << type_i.name() << " index " << index << std::endl;

        if (thread_exception_)
        {
            goby::glog.is(goby::util::logger::WARN) &&
                goby::glog << "Thread type: " << type_i.name() << ", index: " << index
                           << " had an uncaught exception" << std::endl;
            std::rethrow_exception(thread_exception_);
        }
    }
    else
    {
        goby::glog.is(goby::util::logger::DEBUG1) &&
            goby::glog << "Already joined thread: " << type_i.name() << " index " << index
                       << std::endl;
    }
}
} // namespace goby

#endif
