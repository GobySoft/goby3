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
#include "goby/middleware/transport-interprocess-zeromq.h"
#include "goby/middleware/transport-interthread.h"

namespace goby
{
    template<class Config>
        class MultiThreadApplication
        : public goby::common::ApplicationBase3<Config>,
        public goby::Thread<Config, goby::InterProcessPortal<goby::InterThreadTransporter>>
    {
        
    private:
        goby::InterThreadTransporter interthread_;
        goby::InterProcessPortal<decltype(interthread_)> portal_;
        
        std::map<std::type_index, std::atomic<bool>> alive_;
        std::map<std::type_index, std::unique_ptr<std::thread>> threads_;        
        
    public:
        using MainThreadBase = goby::Thread<Config, goby::InterProcessPortal<goby::InterThreadTransporter>>;
        
        using ThreadBase = goby::Thread<Config, goby::InterProcessForwarder<goby::InterThreadTransporter>>;

    MultiThreadApplication(double loop_freq_hertz = 0) :
        MultiThreadApplication(loop_freq_hertz*boost::units::si::hertz)
        { }
        
    MultiThreadApplication(boost::units::quantity<boost::units::si::frequency> loop_freq)
        : MainThreadBase(goby::common::ApplicationBase3<Config>::app_cfg(), &portal_, loop_freq),
            portal_(goby::common::ApplicationBase3<Config>::app_cfg().interprocess_portal())
        {
            goby::glog.set_lock_action(goby::common::logger_lock::lock);
        }
        virtual ~MultiThreadApplication() { }

        
        template<typename ThreadType>
            void launch_thread();
        template<typename ThreadType>
            void join_thread();
        
    protected:
        decltype(portal_)& transporter() { return portal_; }        
        void quit();
        
    private:
        void run() override
        { MainThreadBase::run_once(); }

    };
}

template<class Config>
template<typename ThreadType>
void goby::MultiThreadApplication<Config>::launch_thread()
{
    std::type_index type_i = std::type_index(typeid(ThreadType));
    
    if(threads_.count(type_i))
        return;

    
    alive_[type_i] = true;

    const Config& cfg = goby::common::ApplicationBase3<Config>::app_cfg();
    threads_.insert(
        std::make_pair(type_i,
                       std::unique_ptr<std::thread>(
                           new std::thread([this, type_i, &cfg]()
                                           {
                                               goby::InterProcessForwarder<decltype(interthread_)> forwarder(interthread_);
                                               ThreadType goby_thread(cfg, &forwarder);
                                               goby_thread.run(alive_[type_i]);
                                           }))));
}

template<class Config>
template<typename ThreadType>
void goby::MultiThreadApplication<Config>::join_thread()
{
    auto type_i = std::type_index(typeid(ThreadType));
    if(!threads_.count(type_i))
        return;

    alive_[type_i] = false;
    threads_[type_i]->join();
    alive_.erase(type_i);
    threads_.erase(type_i);
}


template<class Config>
void goby::MultiThreadApplication<Config>::quit()
{
    for(auto& a : alive_)
        a.second = false;
    
    for(auto& t : threads_)
        t.second->join();
    goby::common::ApplicationBase3<Config>::quit();
}

#endif
