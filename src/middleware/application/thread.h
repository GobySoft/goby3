// Copyright 2016-2020:
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

#ifndef THREAD20170616H
#define THREAD20170616H

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <typeindex>

#include <boost/units/systems/si.hpp>

#include "goby/exception.h"
#include "goby/middleware/marshalling/interface.h"
#include "goby/middleware/protobuf/coroner.pb.h"

#include "goby/middleware/common.h"
#include "goby/middleware/group.h"
#include "goby/time/simulation.h"

namespace goby
{
namespace middleware
{
struct ThreadIdentifier
{
    std::type_index type_i{std::type_index(typeid(void))};
    int index{-1};
    bool all_threads{false};
};

/// \brief Represents a thread of execution within the Goby middleware, interleaving periodic events (loop()) with asynchronous receipt of data. Most user code should inherit from SimpleThread, not from Thread directly.
///
/// A Thread can represent the main thread of an application or a thread that was launched after startup.
/// \tparam Config Type of the configuration for thie code running in this Thread
/// \tparam TransporterType Type of the underlying transporter used for publish/subscribe from this thread
template <typename Config, typename TransporterType> class Thread
{
  private:
    TransporterType* transporter_{nullptr};

    boost::units::quantity<boost::units::si::frequency> loop_frequency_;
    std::chrono::system_clock::time_point loop_time_;
    unsigned long long loop_count_{0};
    const Config cfg_;
    int index_;
    std::atomic<bool>* alive_{nullptr};
    std::type_index type_i_{std::type_index(typeid(void))};
    std::string thread_id_;

  public:
    using Transporter = TransporterType;

    /// \brief Construct a thread with a given configuration, underlying transporter, and index (for multiple instantiations), but without any loop() frequency
    ///
    /// \param cfg Data to configure the code running in this thread
    /// \param transporter Underlying transporter
    /// \param index Numeric index to identify this instantiation of the Thread (only necessary if multiple Threads of the same type are created)
    /// Note: loop() will never be called when using this constructor
    Thread(const Config& cfg, TransporterType* transporter, int index)
        : Thread(cfg, transporter, 0 * boost::units::si::hertz, index)
    {
    }

    /// \brief Construct a thread with all possible metadata (using double to specify frequency in Hertz)
    ///
    /// \param cfg Data to configure the code running in this thread
    /// \param transporter Underlying transporter
    /// \param loop_freq_hertz The frequency at which to attempt to call loop(), assuming the thread isn't blocked handling transporter callbacks (e.g. subscribe callbacks)
    /// \param index Numeric index to identify this instantiation of the Thread (only necessary if multiple Threads of the same type are created)
    Thread(const Config& cfg, TransporterType* transporter, double loop_freq_hertz = 0,
           int index = -1)
        : Thread(cfg, transporter, loop_freq_hertz * boost::units::si::hertz, index)
    {
    }

    /// \brief Construct a thread with all possible metadata (using boost::units to specify frequency)
    ///
    /// \param cfg Data to configure the code running in this thread
    /// \param transporter Underlying transporter
    /// \param loop_freq The frequency at which to attempt to call loop(), assuming the thread isn't blocked handling transporter callbacks (i.e. subscribe callbacks)
    /// \param index Numeric index to identify this instantiation of the Thread (only necessary if multiple Threads of the same type are created)
    Thread(const Config& cfg, TransporterType* transporter,
           boost::units::quantity<boost::units::si::frequency> loop_freq, int index = -1)
        : Thread(cfg, loop_freq, index)
    {
        set_transporter(transporter);
    }

    virtual ~Thread() {}

    /// \brief Run the thread until the boolean reference passed is set false. This call blocks, and should be run in a std::thread by the caller.
    ///
    /// \param alive Reference to an atomic boolean. While alive is true, the thread will run; when alive is set false, the thread will complete (and become joinable), assuming nothing is blocking loop() or any transporter callback.
    void run(std::atomic<bool>& alive)
    {
        alive_ = &alive;
        do_subscribe();
        initialize();
        while (alive) { run_once(); }
        finalize();
    }

    /// \return the Thread index (for multiple instantiations)
    int index() const { return index_; }

    std::type_index type_index() { return type_i_; }
    void set_type_index(std::type_index type_i) { type_i_ = type_i; }

    static constexpr goby::middleware::Group shutdown_group_{"goby::middleware::Thread::shutdown"};
    static constexpr goby::middleware::Group joinable_group_{"goby::middleware::Thread::joinable"};

  protected:
    Thread(const Config& cfg, boost::units::quantity<boost::units::si::frequency> loop_freq,
           int index = -1)
        : loop_frequency_(loop_freq),
          loop_time_(std::chrono::system_clock::now()),
          cfg_(cfg),
          index_(index),
          thread_id_(thread_id())
    {
        if (loop_frequency_hertz() > 0 &&
            loop_frequency_hertz() != std::numeric_limits<double>::infinity())
        {
            unsigned long long microsec_interval = 1000000.0 / loop_frequency_hertz();

            unsigned long long ticks_since_epoch =
                std::chrono::duration_cast<std::chrono::microseconds>(loop_time_.time_since_epoch())
                    .count() /
                microsec_interval;

            loop_time_ = std::chrono::system_clock::time_point(
                std::chrono::microseconds((ticks_since_epoch + 1) * microsec_interval));
        }
    }

    void set_transporter(TransporterType* transporter) { transporter_ = transporter; }

    virtual void loop()
    {
        throw(std::runtime_error(
            "void Thread::loop() must be overridden for non-zero loop frequencies"));
    }

    double loop_frequency_hertz() const { return loop_frequency_ / boost::units::si::hertz; }
    decltype(loop_frequency_) loop_frequency() const { return loop_frequency_; }
    double loop_max_frequency() const { return std::numeric_limits<double>::infinity(); }
    void run_once();

    TransporterType& transporter() const { return *transporter_; }

    const Config& cfg() const { return cfg_; }

    // called after alive() is true, but before run()
    virtual void initialize() {}

    // called after alive() is false
    virtual void finalize() {}

    void thread_health(goby::middleware::protobuf::ThreadHealth& health)
    {
        health.set_thread_id(thread_id_);
        health.set_name(health.thread_id());
        this->health(health);
    }

    /// \brief Called when HealthRequest is made by goby_coroner
    ///
    /// Override to implement thread specific health response
    virtual void health(goby::middleware::protobuf::ThreadHealth& health)
    {
        health.set_state(goby::middleware::protobuf::HEALTH__OK);
    }

    void thread_quit()
    {
        (*alive_) = false;
    }

    bool alive() { return alive_ && *alive_; }

  private:
    void do_subscribe()
    {
        if (!transporter_)
        {
            throw(goby::Exception(
                "Thread::transporter_ is null. Must set_transporter() before using"));
        }

        transporter()
            .innermost()
            .template subscribe<shutdown_group_, ThreadIdentifier, MarshallingScheme::CXX_OBJECT>(
                [this](const ThreadIdentifier ti) {

                    if (ti.all_threads ||
                        (ti.type_i == this->type_index() && ti.index == this->index()))
                        this->thread_quit();
                });
    }
};

} // namespace middleware

template <typename Config, typename TransporterType>
constexpr goby::middleware::Group
    goby::middleware::Thread<Config, TransporterType>::shutdown_group_;

template <typename Config, typename TransporterType>
constexpr goby::middleware::Group
    goby::middleware::Thread<Config, TransporterType>::joinable_group_;

template <typename Config, typename TransporterType>
void goby::middleware::Thread<Config, TransporterType>::run_once()
{
    if (!transporter_)
        throw(goby::Exception("Null transporter"));

    if (loop_frequency_hertz() == std::numeric_limits<double>::infinity())
    {
        // call loop as fast as possible
        transporter_->poll(std::chrono::seconds(0));
        loop();
    }
    else if (loop_frequency_hertz() > 0)
    {
        int events = transporter_->poll(loop_time_);

        // timeout
        if (events == 0)
        {
            loop();
            ++loop_count_;
            loop_time_ += std::chrono::nanoseconds(
                (unsigned long long)(1000000000ull / (loop_frequency_hertz() *
                                                      time::SimulatorSettings::warp_factor)));
        }
    }
    else
    {
        // don't call loop()
        transporter_->poll();
    }
}
} // namespace goby

#endif
