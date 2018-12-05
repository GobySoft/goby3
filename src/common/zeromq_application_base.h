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

#ifndef ZEROMQAPPLICATIONBASE20110418H
#define ZEROMQAPPLICATIONBASE20110418H

#include "application_base.h"
#include "zeromq_service.h"

#include "goby/common/logger.h"
#include "goby/common/time.h"

namespace goby
{
namespace common
{
class ZeroMQApplicationBase : public goby::common::ApplicationBase
{
  protected:
    ZeroMQApplicationBase(ZeroMQService* service, google::protobuf::Message* cfg = 0)
        : ApplicationBase(cfg), zeromq_service_(*service)
    {
        set_loop_freq(base_cfg().loop_freq());

        // we are started
        t_start_ = goby::common::goby_time();
        // start the loop() on the next even second
        t_next_loop_ =
            boost::posix_time::second_clock::universal_time() + boost::posix_time::seconds(1);
    }

    virtual ~ZeroMQApplicationBase() {}

    virtual void loop() = 0;

    /// \brief set the interval (with a boost::posix_time::time_duration) between calls to loop. Alternative to set_loop_freq().
    ///
    /// \param p new interval between calls to loop()
    void set_loop_period(boost::posix_time::time_duration p) { loop_period_ = p; }
    /// \brief set the interval in milliseconds between calls to loop. Alternative to set_loop_freq().
    ///
    /// \param milliseconds new period for loop() synchronous event
    void set_loop_period(long milliseconds)
    {
        set_loop_period(boost::posix_time::milliseconds(milliseconds));
    }

    /// \brief set the frequency with which loop() is called. Alternative to set_loop_period().
    ///
    /// \param hertz new frequency for loop()
    void set_loop_freq(double hertz)
    {
        set_loop_period(boost::posix_time::milliseconds(1000.0 / hertz));
    }

    /// interval between calls to loop()
    boost::posix_time::time_duration loop_period() { return loop_period_; }
    /// frequency of calls to loop() in Hertz
    long loop_freq() { return 1000 / loop_period_.total_milliseconds(); }
    /// \return absolute time that this application was launched
    boost::posix_time::ptime t_start() { return t_start_; }

  private:
    void iterate()
    {
        using goby::glog;

        // sit and wait on a message until the next time to call loop() is up
        long timeout = (t_next_loop_ - goby::common::goby_time()).total_microseconds();
        if (timeout < 0)
            timeout = 0;

        glog.is(goby::common::logger::DEBUG3) && glog << "timeout set to: " << timeout
                                                      << " microseconds." << std::endl;
        bool had_events = zeromq_service_.poll(timeout);
        if (!had_events)
        {
            // no message, time to call loop()
            loop();
            t_next_loop_ += loop_period_;
        }
    }

  private:
    ZeroMQService& zeromq_service_;

    // how long to wait between calls to loop()
    boost::posix_time::time_duration loop_period_;

    // time this process was started
    boost::posix_time::ptime t_start_;
    // time of the next call to loop()
    boost::posix_time::ptime t_next_loop_;
};
} // namespace common
} // namespace goby

#endif
