// Copyright 2017-2023:
//   GobySoft, LLC (2013-)
//   Community contributors (see AUTHORS file)
// File authors:
//   Toby Schneider <toby@gobysoft.org>
//   James D. Turner <james.turner@nrl.navy.mil>
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

#ifndef GOBY_MIDDLEWARE_APPLICATION_MULTI_THREAD_H
#define GOBY_MIDDLEWARE_APPLICATION_MULTI_THREAD_H

#include <boost/core/demangle.hpp>
#include <boost/units/systems/si.hpp>

#include "goby/middleware/coroner/coroner.h"
#include "goby/middleware/navigation/navigation.h"
#include "goby/middleware/terminate/terminate.h"

#include "goby/exception.h"
#include "goby/middleware/application/detail/interprocess_common.h"
#include "goby/middleware/application/detail/thread_type_selector.h"
#include "goby/middleware/application/groups.h"
#include "goby/middleware/application/interface.h"
#include "goby/middleware/application/simple_thread.h"
#include "goby/middleware/application/thread.h"

#include "goby/middleware/transport/interprocess.h"
#include "goby/middleware/transport/interthread.h"
#include "goby/middleware/transport/intervehicle.h"

namespace goby
{
namespace middleware
{
/// \brief Thread that simply publishes an empty message on its loop interval to TimerThread::group
///
/// This can be launched to provide a simple timer by subscribing to TimerThread::group.
///
/// For example, to create timer that expires every two seconds:
/// ```
/// launch_thread<goby::middleware::TimerThread<0>>(0.5*boost::units::si::hertz);
/// interthread().subscribe_empty<goby::middleware::TimerThread<0>::expire_group>([]() { std::cout << "Timer expired." << std::endl; });
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
        int uid;
        std::unique_ptr<std::thread> thread;
    };

    static std::exception_ptr thread_exception_;

    std::map<std::type_index, std::map<int, ThreadManagement>> threads_;
    int thread_uid_{0};
    int running_thread_count_{0};
    InterThreadTransporter interthread_;

  public:
    template <typename ThreadType> void launch_thread()
    {
        _launch_thread<ThreadType, Config, false, true>(-1, this->app_cfg());
    }
    template <typename ThreadType> void launch_thread(int index)
    {
        _launch_thread<ThreadType, Config, true, true>(index, this->app_cfg());
    }

    template <typename ThreadType, typename ThreadConfig>
    void launch_thread(const ThreadConfig& cfg)
    {
        _launch_thread<ThreadType, ThreadConfig, false, true>(-1, cfg);
    }
    template <typename ThreadType, typename ThreadConfig>
    void launch_thread(int index, const ThreadConfig& cfg)
    {
        _launch_thread<ThreadType, ThreadConfig, true, true>(index, cfg);
    }

    template <typename ThreadType> void launch_thread_without_cfg()
    {
        _launch_thread<ThreadType, Config, false, false>(-1, this->app_cfg());
    }
    template <typename ThreadType> void launch_thread_without_cfg(int index)
    {
        _launch_thread<ThreadType, Config, true, false>(index, this->app_cfg());
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
    virtual void post_finalize() override { join_all_threads(); }

    std::map<std::type_index, std::map<int, ThreadManagement>>& threads() { return threads_; }

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

    template <typename ThreadType, typename ThreadConfig, bool has_index, bool has_config>
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

    protobuf::ProcessHealth health_response_;

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
          interprocess_(Base::interthread(), detail::make_interprocess_config(
                                                 this->app_cfg().interprocess(), this->app_name())),
          intervehicle_(interprocess_)
    {
        // handle goby_terminate request
        this->interprocess().template subscribe<groups::terminate_request>(
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

        // handle request from HealthMonitor thread
        health_response_.set_name(this->app_name());
        health_response_.set_pid(getpid());

        this->interthread().template subscribe<groups::health_request>(
            [this](const protobuf::HealthRequest& request) {
                auto health_response = std::make_shared<protobuf::ProcessHealth>(health_response_);
                // preseed all threads with error in case they don't respond
                for (const auto& type_map_p : this->threads())
                {
                    for (const auto& index_manager_p : type_map_p.second)
                    {
                        const auto& thread_manager = index_manager_p.second;
                        auto& thread_health = *health_response->mutable_main()->add_child();
                        thread_health.set_name(thread_manager.name);
                        thread_health.set_uid(thread_manager.uid);
                        thread_health.set_state(protobuf::HEALTH__FAILED);
                        thread_health.set_error(protobuf::ERROR__THREAD_NOT_RESPONDING);
                    }
                }

                this->thread_health(*health_response->mutable_main());
                this->interthread().template publish<groups::health_response>(health_response);
            });

        this->interprocess().template subscribe<goby::middleware::groups::datum_update>(
            [this](const protobuf::DatumUpdate& datum_update) {
                this->configure_geodesy(
                    {datum_update.datum().lat_with_units(), datum_update.datum().lon_with_units()});
            });

        this->interprocess().template publish<goby::middleware::groups::configuration>(
            this->app_cfg());

        if (this->app_cfg().app().health_cfg().run_health_monitor_thread())
            this->template launch_thread_without_cfg<HealthMonitorThread>();
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

    /// \brief Assume all required subscriptions are done in the Constructor or in initialize(). If this isn't the case, this method can be overridden
    virtual void post_initialize() override { interprocess().ready(); };
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
template <typename ThreadType, typename ThreadConfig, bool has_index, bool has_config>
void goby::middleware::MultiThreadApplicationBase<Config, Transporter>::_launch_thread(
    int index, const ThreadConfig& cfg)
{
    std::type_index type_i = std::type_index(typeid(ThreadType));

    if (threads_[type_i].count(index) && threads_[type_i][index].alive)
        throw(Exception(std::string("Thread of type: ") + type_i.name() + " and index " +
                        std::to_string(index) + " is already launched and running."));

    auto& thread_manager = threads_[type_i][index];
    thread_manager.alive = true;
    thread_manager.name = boost::core::demangle(typeid(ThreadType).name());
    if (has_index)
        thread_manager.name += "/" + std::to_string(index);
    thread_manager.uid = thread_uid_++;

    // copy configuration
    auto thread_lambda = [this, type_i, index, cfg, &thread_manager]() {
#ifdef __APPLE__
        // set thread name for debugging purposes
        pthread_setname_np(thread_manager.name.c_str());
#endif
        try
        {
            std::shared_ptr<ThreadType> goby_thread(
                detail::ThreadTypeSelector<ThreadType, ThreadConfig, has_index, has_config>::thread(
                    cfg, index));

            goby_thread->set_name(thread_manager.name);
            goby_thread->set_type_index(type_i);
            goby_thread->set_uid(thread_manager.uid);
            goby_thread->run(thread_manager.alive);
        }
        catch (...)
        {
            thread_exception_ = std::current_exception();
        }

        interthread_.publish<MainThreadBase::joinable_group_>(ThreadIdentifier{type_i, index});
    };

    thread_manager.thread = std::unique_ptr<std::thread>(new std::thread(thread_lambda));

#ifndef __APPLE__
    // set thread name for debugging purposes
    pthread_setname_np(thread_manager.thread->native_handle(), thread_manager.name.c_str());
#endif

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
