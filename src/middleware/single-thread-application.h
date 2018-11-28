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

#ifndef SINGLETHREADAPPLICATION20161120H
#define SINGLETHREADAPPLICATION20161120H

#include <boost/units/systems/si.hpp>

#include "goby/common/application_base3.h"
#include "goby/middleware/thread.h"
#include "goby/middleware/transport-interprocess-zeromq.h"
#include "goby/middleware/transport-intervehicle.h"

#include "goby/middleware/protobuf/terminate.pb.h"
#include "goby/middleware/terminate/groups.h"

namespace goby
{
    template<class Config, class StateMachine = goby::common::NullStateMachine> 
        class SingleThreadApplication : public goby::common::ApplicationBase3<Config, StateMachine>, public Thread<Config, InterVehicleForwarder<InterProcessPortal<>>>
    {
    private:
        using MainThread = Thread<Config, InterVehicleForwarder<InterProcessPortal<>>>;
        
        InterProcessPortal<> interprocess_;
        InterVehicleForwarder<InterProcessPortal<>> intervehicle_;
        
    public:
    SingleThreadApplication(double loop_freq_hertz = 0) :
        SingleThreadApplication(loop_freq_hertz*boost::units::si::hertz)
        { }
        
    SingleThreadApplication(boost::units::quantity<boost::units::si::frequency> loop_freq)
        : MainThread(this->app_cfg(), loop_freq),
          interprocess_(this->app_cfg().interprocess()),
          intervehicle_(interprocess_)
        {
	    this->set_transporter(&intervehicle_);

            // handle goby_terminate request
            this->interprocess().template subscribe<groups::terminate_request, protobuf::TerminateRequest>(
                [this](const protobuf::TerminateRequest& request) {
                    if(request.target_name() == this->app_cfg().app().name())
                    {
                        goby::glog.is_debug2() && goby::glog << "Received request to cleanly quit() from goby_terminate" << std::endl;
                        
                        protobuf::TerminateResponse resp;
                        resp.set_target_name(this->app_cfg().app().name());
                        this->interprocess().template publish<groups::terminate_response>(resp);
                        this->quit();
                    }
                }
                );
        }
        
        virtual ~SingleThreadApplication() { }
        
    protected:            
        InterProcessPortal<>& interprocess() { return interprocess_; } 
        InterVehicleForwarder<InterProcessPortal<>>& intervehicle() { return intervehicle_; }
    
    private:
        void run() override
        { MainThread::run_once(); }
        
    };

    
    
}

#endif
