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

#ifndef THREAD20170616H
#define THREAD20170616H

#include <memory>
#include <atomic>

namespace goby
{
    template<typename Config, typename TransporterType>
        class Thread
    {
    private:
        TransporterType* transporter_;
        
        boost::units::quantity<boost::units::si::frequency> loop_frequency_;
        std::chrono::system_clock::time_point loop_time_;
        unsigned long long loop_count_{0};
        const Config& cfg_;
        unsigned index_{0};

    public:
        using Transporter = TransporterType;
        
        // zero or negative frequency means loop() is never called
    Thread(const Config& cfg, TransporterType* transporter, double loop_freq_hertz = 0) :
        Thread(cfg, transporter, loop_freq_hertz*boost::units::si::hertz) { }
        
    Thread(const Config& cfg, TransporterType* transporter,
           boost::units::quantity<boost::units::si::frequency> loop_freq)
        : Thread(cfg, loop_freq)
        {
            set_transporter(transporter);
        }

        virtual ~Thread() { }
        
        void run(std::atomic<bool>& alive)
        { while(alive) run_once(); }

        void set_index(unsigned index) { index_ = index; }
        unsigned index() { return index_; }
        
        
    protected:
    Thread(const Config& cfg,
           boost::units::quantity<boost::units::si::frequency> loop_freq)
        : loop_frequency_(loop_freq),
            loop_time_(std::chrono::system_clock::now()),
            cfg_(cfg)
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

        void set_transporter(TransporterType* transporter)
        { transporter_ = transporter; }
        
        virtual void loop() { sleep(1); }

        double loop_frequency_hertz() { return loop_frequency_/boost::units::si::hertz; }
        decltype(loop_frequency_) loop_frequency() { return loop_frequency_; }
        double loop_max_frequency() { return std::numeric_limits<double>::infinity(); }
        
        void run_once();

        TransporterType& transporter() { return *transporter_; }

        const Config& cfg() { return cfg_; }
    };


}


template<typename Config, typename TransporterType>
    void goby::Thread<Config, TransporterType>::run_once()
{
    if(!transporter_)
        throw(goby::Exception("Null transporter"));

    if(loop_frequency_hertz() == std::numeric_limits<double>::infinity())
    {
        // call loop as fast as possible
        transporter_->poll(std::chrono::seconds(0));
        loop();
    }
    else if(loop_frequency_hertz() > 0)
    {
        int events = transporter_->poll(loop_time_);
        
        // timeout
        if(events == 0)
        {
            loop();
            ++loop_count_;
            loop_time_ += std::chrono::nanoseconds((unsigned long long)(1000000000ull / loop_frequency_hertz()));
        }
    }
    else
    {
        // don't call loop()
        transporter_->poll();
    }
}


#endif
