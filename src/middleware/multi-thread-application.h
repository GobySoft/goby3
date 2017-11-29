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

#include "goby/common/exception.h"
#include "goby/common/application_base3.h"
#include "goby/middleware/thread.h"
#include "goby/middleware/transport-interprocess-zeromq.h"
#include "goby/middleware/transport-interthread.h"

namespace goby
{

    template<typename Config>
        class SimpleThread : public Thread<Config, goby::InterProcessForwarder<goby::InterThreadTransporter>>
    {
    public:
    SimpleThread(const Config& cfg, double loop_freq_hertz = 0, int index = -1) :
        SimpleThread(cfg, loop_freq_hertz*boost::units::si::hertz, index) { }
        
    SimpleThread(const Config& cfg, 
           boost::units::quantity<boost::units::si::frequency> loop_freq, int index = -1)
        : Thread<Config, goby::InterProcessForwarder<goby::InterThreadTransporter>>(cfg, loop_freq, index)
        {
            interthread_.reset(new goby::InterThreadTransporter);
            forwarder_.reset(new goby::InterProcessForwarder<goby::InterThreadTransporter>(*interthread_));
            Thread<Config, goby::InterProcessForwarder<goby::InterThreadTransporter>>::set_transporter(forwarder_.get());
        }

        goby::InterProcessForwarder<goby::InterThreadTransporter>& interprocess()
        {
            return Thread<Config, goby::InterProcessForwarder<goby::InterThreadTransporter>>::transporter();
        }

        goby::InterThreadTransporter& interthread()
        {
            return Thread<Config, goby::InterProcessForwarder<goby::InterThreadTransporter>>::transporter().inner();
        }
        
    private:
        std::unique_ptr<goby::InterThreadTransporter> interthread_;
        std::unique_ptr<goby::InterProcessForwarder<goby::InterThreadTransporter>> forwarder_;
        
    };

    template<class Config, class Transporter>
        class MultiThreadApplicationBase
        : public goby::common::ApplicationBase3<Config>,
        public goby::Thread<Config, Transporter>
    {
        
    private:
        std::map<std::type_index, std::map<int, std::atomic<bool>>> alive_;
        std::map<std::type_index, std::map<int, std::unique_ptr<std::thread>>> threads_;        

		
    public:
        using MainThreadBase = goby::Thread<Config, Transporter>;
        
    MultiThreadApplicationBase(double loop_freq_hertz = 0) :
        MultiThreadApplicationBase(loop_freq_hertz*boost::units::si::hertz)
        { }


        MultiThreadApplicationBase(boost::units::quantity<boost::units::si::frequency> loop_freq, Transporter* portal)
    : MainThreadBase(goby::common::ApplicationBase3<Config>::app_cfg(), portal, loop_freq)
    {

        goby::glog.set_lock_action(goby::common::logger_lock::lock);
    }
        virtual ~MultiThreadApplicationBase() { }

        
        template<typename ThreadType>
            void launch_thread();
        template<typename ThreadType>
            void launch_thread(int index);
        template<typename ThreadType>
            void join_thread(int index = -1);
        
    protected:        
        void quit() override;
        
    private:

    void run() override
        { MainThreadBase::run_once(); }

	template<typename ThreadType, typename Lambda>
            void _launch_thread(int index, Lambda thread_lambda);

    };

    
    template<class Config>
        class MultiThreadApplication
        : public MultiThreadApplicationBase<Config, goby::InterProcessPortal<goby::InterThreadTransporter>>
    {
        
    private:
        goby::InterThreadTransporter interthread_;        
        goby::InterProcessPortal<goby::InterThreadTransporter> portal_;
        using Base = MultiThreadApplicationBase<Config, goby::InterProcessPortal<goby::InterThreadTransporter>>;
        
    public:
    MultiThreadApplication(double loop_freq_hertz = 0) :
        MultiThreadApplication(loop_freq_hertz*boost::units::si::hertz)
        { }
        
    MultiThreadApplication(boost::units::quantity<boost::units::si::frequency> loop_freq)
        : Base(loop_freq, &portal_),
            portal_(interthread_, goby::common::ApplicationBase3<Config>::app_cfg().interprocess())
        { }
        virtual ~MultiThreadApplication() { }

    protected:
        goby::InterThreadTransporter& interthread() { return portal_.inner(); }
        goby::InterProcessPortal<goby::InterThreadTransporter>& interprocess() { return portal_; }

    };

    template<class Config>
        class MultiThreadStandaloneApplication
        : public MultiThreadApplicationBase<Config, goby::InterThreadTransporter>
    {
        
    private:
        using Base = MultiThreadApplicationBase<Config, goby::InterThreadTransporter>;
        goby::InterThreadTransporter interthread_;        
        
    public:
    MultiThreadStandaloneApplication(double loop_freq_hertz = 0) :
        MultiThreadStandaloneApplication(loop_freq_hertz*boost::units::si::hertz)
        { }
        
    MultiThreadStandaloneApplication(boost::units::quantity<boost::units::si::frequency> loop_freq)
        : Base(loop_freq, &interthread_)
        {
            
        }
        virtual ~MultiThreadStandaloneApplication() { }

    protected:
        goby::InterThreadTransporter& interthread() { return interthread_; }

    };

    template<class Config>
        class MultiThreadTest
        : public MultiThreadStandaloneApplication<Config>
    {
        
    private:
        using Base = MultiThreadStandaloneApplication<Config>;
        
    public:
    MultiThreadTest(boost::units::quantity<boost::units::si::frequency> loop_freq = 0*boost::units::si::hertz)
        : Base(loop_freq)
        {
            
        }
        virtual ~MultiThreadTest() { }

    protected:
        // so we can add on threads that publish to the outside for testing
        goby::InterThreadTransporter& interprocess() { return Base::interthread(); }

    };

    
}

template<class Config, class Transporter>
template<typename ThreadType>
void goby::MultiThreadApplicationBase<Config, Transporter>::launch_thread()
{
    const Config& cfg = goby::common::ApplicationBase3<Config>::app_cfg();
    int index = -1;
    std::type_index type_i = std::type_index(typeid(ThreadType));
    auto thread_lambda = [this, type_i, index, &cfg]()
	{
	    ThreadType goby_thread(cfg);
	    goby_thread.run(alive_[type_i][index]);
	};
    _launch_thread<ThreadType>(index, thread_lambda);
}

template<class Config, class Transporter>
template<typename ThreadType>
void goby::MultiThreadApplicationBase<Config, Transporter>::launch_thread(int index)
{
    const Config& cfg = goby::common::ApplicationBase3<Config>::app_cfg();
    std::type_index type_i = std::type_index(typeid(ThreadType));
    auto thread_lambda = [this, type_i, index, &cfg]()
	{
	    ThreadType goby_thread(cfg, index);
	    goby_thread.run(alive_[type_i][index]);
	};
    _launch_thread<ThreadType>(index, thread_lambda);
}

template<class Config, class Transporter>
template<typename ThreadType, typename Lambda>
    void goby::MultiThreadApplicationBase<Config, Transporter>::_launch_thread(int index, Lambda thread_lambda)
{   
    std::type_index type_i = std::type_index(typeid(ThreadType));
    
    if(threads_[type_i].count(index))
        throw(Exception(std::string("Thread of type: ") + type_i.name() + " and index " + std::to_string(index) + " was already launched."));
    
    alive_[type_i][index] = true;

    threads_[type_i].insert(
        std::make_pair(index,
                       std::unique_ptr<std::thread>(new std::thread(thread_lambda))));    
}


template<class Config, class Transporter>
template<typename ThreadType>
void goby::MultiThreadApplicationBase<Config, Transporter>::join_thread(int index /* = -1 */)
{
    auto type_i = std::type_index(typeid(ThreadType));

    if(!threads_[type_i].count(index))
        throw(Exception(std::string("No running thread of type: ") + type_i.name() + " and index " + std::to_string(index) + " to join."));

    alive_[type_i][index] = false;
    threads_[type_i][index]->join();
    alive_[type_i].erase(index);
    threads_[type_i].erase(index);
}


template<class Config, class Transporter>
void goby::MultiThreadApplicationBase<Config, Transporter>::quit()
{
    for(auto& amap : alive_)
    {
        for (auto& a : amap.second)
            a.second = false;
    }
    
    for(auto& tmap : threads_)
    {
        for(auto & t : tmap.second)
            t.second->join();
    }
    
    goby::common::ApplicationBase3<Config>::quit();
}

#endif
