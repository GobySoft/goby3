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

#include "logger_manipulators.h"
#include "flex_ostream.h"
#include "goby/common/time.h"

std::ostream & operator<< (std::ostream& os, const Group & g)
{
    os << "description: " << g.description() << std::endl;
    os << "color: " << goby::common::TermColor::str_from_col(g.color());
    return os;
}

void GroupSetter::operator()(goby::common::FlexOstream& os) const
{    
    os.set_group(group_);
}

void GroupSetter::operator()(std::ostream& os) const
{
    try
    {
        goby::common::FlexOstream& flex = dynamic_cast<goby::common::FlexOstream&>(os);
        flex.set_group(group_);
    }
    catch(...)
    {
        basic_log_header(os, group_);
    }
}

std::ostream& basic_log_header(std::ostream& os, const std::string& group_name)
{
    os << "[ " << goby::common::goby_time_as_string() << " ]";

    if(!group_name.empty())
        os << " " << std::setfill(' ') << std::setw(15) << "{" << group_name << "}";
        
    os << ": ";
    return os;
}

goby::common::FlexOstream& operator<<(goby::common::FlexOstream& os, const GroupSetter & gs)
{
    os.set_unset_verbosity();
    gs(os);
    return(os);
}
