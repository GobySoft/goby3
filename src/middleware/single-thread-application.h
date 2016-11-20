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

#ifndef SINGLETHREADAPPLICATION20161120H
#define SINGLETHREADAPPLICATION20161120H

#include <boost/units/systems/si.hpp>

#include "goby/common/application_base3.h"
#include "goby/middleware/transport-interprocess.h"

namespace goby
{
    template<class Config>
        class SingleThreadApplication : public goby::common::ApplicationBase3<Config>
    {        
        
    private:
        boost::units::quantity<boost::units::si::frequency> loop_frequency_;
        goby::InterProcessPortal<> portal_;
        std::chrono::system_clock::time_point loop_time_;
        unsigned long long loop_count_ {0};

    public:
    SingleThreadApplication(double loop_freq_hertz) :
        SingleThreadApplication(loop_freq_hertz*boost::units::si::hertz)
        { }
        
    SingleThreadApplication(boost::units::quantity<boost::units::si::frequency> loop_freq = 1*boost::units::si::hertz)
        : loop_frequency_(loop_freq),
            portal_(goby::common::ApplicationBase3<Config>::cfg().interprocess_portal()),
            loop_time_(goby::common::ApplicationBase3<Config>::start_time())
        {
            unsigned long long ticks_since_epoch =
                std::chrono::duration_cast<std::chrono::microseconds>(
                    loop_time_.time_since_epoch()).count() /
                (1000000ull/loop_frequency_hertz());
            
            loop_time_ =
                std::chrono::system_clock::time_point(
                    std::chrono::microseconds(
                        (ticks_since_epoch+1)*
                        (unsigned long long)(1000000ull/
                                             loop_frequency_hertz()))); 
        }

        virtual ~SingleThreadApplication() { }
        
    protected:            
        goby::InterProcessPortal<>& portal() { return portal_; }        
        virtual void loop() = 0;

        double loop_frequency_hertz() { return loop_frequency_/boost::units::si::hertz; }
        decltype(loop_frequency_) loop_frequency() { return loop_frequency_; }
        
            
        
    private:
        void run() override;
        
    };
}

template<class Config>
    void goby::SingleThreadApplication<Config>::run()
{
    int events = portal_.poll(loop_time_);
    
    // timeout
    if(events == 0)
    {
        loop();
        ++loop_count_;
        loop_time_ += std::chrono::nanoseconds((unsigned long long)(1000000000ull / loop_frequency_hertz()));
    }    
}

#endif
