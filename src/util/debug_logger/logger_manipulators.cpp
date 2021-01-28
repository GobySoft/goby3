// Copyright 2012-2020:
//   GobySoft, LLC (2013-)
//   Massachusetts Institute of Technology (2007-2014)
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

#include <boost/date_time/gregorian/gregorian.hpp>
#include <boost/date_time/posix_time/posix_time_io.hpp> // for operator<<
#include <chrono>                                       // for time_point
#include <iomanip>                                      // for operator<<
#include <iterator>                                     // for ostreambuf_i...
#include <sstream>                                      // for basic_string...

#include "goby/time/convert.h" // for str

#include "flex_ostream.h" // for FlexOstream
#include "logger_manipulators.h"

std::ostream& goby::util::logger::operator<<(std::ostream& os, const goby::util::logger::Group& g)
{
    os << "description: " << g.description() << std::endl;
    os << "color: " << goby::util::TermColor::str_from_col(g.color());
    return os;
}

void goby::util::logger::GroupSetter::operator()(goby::util::FlexOstream& os) const
{
    os.set_group(group_);
}

void goby::util::logger::GroupSetter::operator()(std::ostream& os) const
{
    try
    {
        auto& flex = dynamic_cast<goby::util::FlexOstream&>(os);
        flex.set_group(group_);
    }
    catch (...)
    {
        basic_log_header(os, group_);
    }
}

std::ostream& goby::util::logger::basic_log_header(std::ostream& os, const std::string& group_name)
{
    os << "[ " << goby::time::str() << " ]";

    if (!group_name.empty())
        os << " " << std::setfill(' ') << std::setw(15) << "{" << group_name << "}";

    os << ": ";
    return os;
}

goby::util::FlexOstream& goby::util::logger::operator<<(goby::util::FlexOstream& os,
                                                        const GroupSetter& gs)
{
    //    os.set_unset_verbosity();
    gs(os);
    return (os);
}
