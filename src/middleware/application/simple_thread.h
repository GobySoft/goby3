// Copyright 2022-2024:
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

#ifndef GOBY_MIDDLEWARE_APPLICATION_SIMPLE_THREAD_H
#define GOBY_MIDDLEWARE_APPLICATION_SIMPLE_THREAD_H

#include "goby/middleware/application/thread.h"
#include "goby/middleware/coroner/coroner.h"

#include "goby/middleware/transport/interprocess.h"
#include "goby/middleware/transport/interthread.h"
#include "goby/middleware/transport/intervehicle.h"
#include "goby/zeromq/transport/detail/tags.h"

namespace goby
{
namespace middleware
{
/// \brief Implements Thread for a three layer middleware setup ([ intervehicle [ interprocess [ interthread ] ] ]) based around InterVehicleForwarder.
///
/// \tparam Config Configuration type
/// \tparam ImplementationTag Tag type selecting the interprocess implementation (e.g. zeromq::detail::ZeromqInterprocessTag). Defaults to void (deprecated; use zeromq::SimpleThread or udpm::SimpleThread instead).
///
/// Derive from this class to create standalone threads that can be launched and joined by MultiThreadApplication's launch_thread and join_thread methods.
template <typename Config, typename ImplementationTag = void>
class SimpleThread
    : public Thread<Config,
                    InterVehicleForwarder<InterProcessForwarder<InterThreadTransporter,
                                                                ImplementationTag>>>,
      public coroner::Thread<SimpleThread<Config, ImplementationTag>>
{
    using SimpleThreadBase =
        Thread<Config,
               InterVehicleForwarder<
                   InterProcessForwarder<InterThreadTransporter, ImplementationTag>>>;

    friend class coroner::Thread<SimpleThread<Config, ImplementationTag>>;

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
        interprocess_.reset(
            new InterProcessForwarder<InterThreadTransporter, ImplementationTag>(*interthread_));
        intervehicle_.reset(
            new InterVehicleForwarder<InterProcessForwarder<InterThreadTransporter, ImplementationTag>>(
                *interprocess_));

        this->set_transporter(intervehicle_.get());

        this->subscribe_coroner();
    }

    /// \brief Access the transporter on the intervehicle layer (which wraps interprocess and interthread)
    InterVehicleForwarder<InterProcessForwarder<InterThreadTransporter, ImplementationTag>>&
    intervehicle()
    {
        return this->transporter();
    }

    /// \brief Access the transporter on the interprocess layer (which wraps interthread)
    InterProcessForwarder<InterThreadTransporter, ImplementationTag>& interprocess()
    {
        return this->transporter().inner();
    }

    /// \brief Access the transporter on the interthread layer (this is the innermost transporter)
    InterThreadTransporter& interthread() { return this->transporter().innermost(); }

  private:
    std::unique_ptr<InterThreadTransporter> interthread_;
    std::unique_ptr<InterProcessForwarder<InterThreadTransporter, ImplementationTag>> interprocess_;
    std::unique_ptr<
        InterVehicleForwarder<InterProcessForwarder<InterThreadTransporter, ImplementationTag>>>
        intervehicle_;
};

/// \brief Deprecated 1-arg specialisation: use goby::zeromq::SimpleThread or goby::udpm::SimpleThread
template <typename Config>
class SimpleThread<Config, void>
    : public SimpleThread<Config, zeromq::detail::ZeromqInterprocessTag>
{
  public:
    using Base = SimpleThread<Config, zeromq::detail::ZeromqInterprocessTag>;

    [[deprecated(
        "Use goby::zeromq::SimpleThread or goby::udpm::SimpleThread instead of "
        "goby::middleware::SimpleThread")]]
    SimpleThread(const Config& cfg, double loop_freq_hertz = 0, int index = -1)
        : Base(cfg, loop_freq_hertz, index)
    {
    }

    [[deprecated(
        "Use goby::zeromq::SimpleThread or goby::udpm::SimpleThread instead of "
        "goby::middleware::SimpleThread")]]
    SimpleThread(const Config& cfg, boost::units::quantity<boost::units::si::frequency> loop_freq,
                 int index = -1)
        : Base(cfg, loop_freq, index)
    {
    }
};

} // namespace middleware
} // namespace goby

#endif
