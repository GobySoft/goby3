// Copyright 2009-2016 Toby Schneider (http://gobysoft.org/index.wt/people/toby)
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

#include "goby/common/application_base3.h"
#include "goby/middleware/thread.h"
#include "goby/middleware/transport-interprocess.h"
#include "goby/middleware/transport-interthread.h"

namespace goby
{
    template<class Config>
        class MultiThreadApplication
        : public goby::common::ApplicationBase3<Config>,
        public goby::Thread<goby::InterProcessPortal<goby::InterThreadTransporter>>
    {
        
    private:
        goby::InterThreadTransporter interthread_;
        goby::InterProcessPortal<decltype(interthread_)> portal_;
        std::vector<std::unique_ptr<std::thread>> threads_;
        
        std::atomic<bool> alive_{ true };
        
    public:
    MultiThreadApplication(double loop_freq_hertz = 0) :
        MultiThreadApplication(loop_freq_hertz*boost::units::si::hertz)
        { }
        
    MultiThreadApplication(boost::units::quantity<boost::units::si::frequency> loop_freq)
        : Thread(&portal_, loop_freq),
            portal_(goby::common::ApplicationBase3<Config>::cfg().interprocess_portal())
        {
            goby::glog.set_lock_action(goby::common::logger_lock::lock);
        }
        virtual ~MultiThreadApplication() { }

        using ThreadBase = goby::Thread<goby::InterProcessForwarder<decltype(interthread_)>>;
        
        template<typename ThreadType>
            void launch_thread();
        
    protected:
        decltype(portal_)& portal() { return portal_; }        
        virtual void loop() {}
        void quit();
        
    private:
        void run() override
        { Thread::run_once(); }

    };
}

template<class Config>
template<typename ThreadType>
void goby::MultiThreadApplication<Config>::launch_thread()
{
    threads_.push_back(std::unique_ptr<std::thread>(
                           new std::thread([&]()
                                           {
                                               goby::InterProcessForwarder<decltype(interthread_)> forwarder(interthread_);
                                               ThreadType goby_thread(&forwarder);
                                               goby_thread.run(alive_);
                                           })));
}

template<class Config>
void goby::MultiThreadApplication<Config>::quit()
{
    alive_ = false;
    for(auto& t : threads_)
        t->join();
    goby::common::ApplicationBase3<Config>::quit();
}

#endif
