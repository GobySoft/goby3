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

#include "goby/common/time.h"

boost::function0<goby::uint64> goby::common::goby_time_function;
int goby::common::goby_time_warp_factor = 1;

double goby::common::ptime2unix_double(boost::posix_time::ptime given_time)
{
    using namespace boost::posix_time;
    using namespace boost::gregorian;
        
    if (given_time == not_a_date_time)
        return -1;
    else
    {
        date_duration date_diff = given_time.date() - date(1970,1,1);
        time_duration time_diff = given_time.time_of_day();
        
        return
            static_cast<double>(date_diff.days())*24*3600 +
            static_cast<double>(time_diff.total_seconds()) +
            static_cast<double>(time_diff.fractional_seconds()) /
            static_cast<double>(time_duration::ticks_per_second());
    }
}

boost::posix_time::ptime goby::common::unix_double2ptime(double given_time)
{
    using namespace boost::posix_time;
    using namespace boost::gregorian;
    
    if (given_time == -1)
        return boost::posix_time::ptime(not_a_date_time);
    else
    {
        date date_epoch(date(1970,1,1));

        double sec = floor(given_time);
        long micro_s = (given_time - sec)*1e6;
        long d = sec / 3600 / 24;
        sec -= static_cast<double>(d)*3600*24;
        long h = sec / 3600;
        sec -= h*3600;
        long m = sec / 60;
        sec -= m*60;
        long s = sec;        
        return ptime(date_epoch + days(d),
                     time_duration(h, m, s) + microseconds(micro_s));
    }
}

goby::uint64 goby::common::ptime2unix_microsec(boost::posix_time::ptime given_time)
{
    using namespace boost::posix_time;
    using namespace boost::gregorian;
        
    if (given_time == not_a_date_time)
        return std::numeric_limits<uint64>::max();
    else
    {
        const int MICROSEC_IN_SEC = 1000000;

        date_duration date_diff = given_time.date() - date(1970,1,1);
        time_duration time_diff = given_time.time_of_day();
        
        return
            static_cast<uint64>(date_diff.days())*24*3600*MICROSEC_IN_SEC + 
            static_cast<uint64>(time_diff.total_seconds())*MICROSEC_IN_SEC +
            static_cast<uint64>(time_diff.fractional_seconds()) /
            (time_duration::ticks_per_second() / MICROSEC_IN_SEC);        
    }    
}

    
/// convert to boost date_time ptime from the number of microseconds since 1/1/1970 0:00 UTC ("UNIX Time"): good to the microsecond
boost::posix_time::ptime goby::common::unix_microsec2ptime(uint64 given_time)
{
    using namespace boost::posix_time;
    using namespace boost::gregorian;
    
    if (given_time == std::numeric_limits<uint64>::max())
        return boost::posix_time::ptime(not_a_date_time);
    else
    {
        const int MICROSEC_IN_SEC = 1000000;
        ptime time_t_epoch(date(1970,1,1));
        uint64 m = given_time / MICROSEC_IN_SEC / 60;
        uint64 s = (given_time / MICROSEC_IN_SEC) - m*60;
        uint64 micro_s = (given_time - (s + m*60) * MICROSEC_IN_SEC);
        return time_t_epoch + minutes(m) + seconds(s) + microseconds(micro_s);
    }
}
