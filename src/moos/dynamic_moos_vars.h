// Copyright 2009-2017 Toby Schneider (http://gobysoft.org/index.wt/people/toby)
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

#ifndef DYNAMICMOOSVARSH
#define DYNAMICMOOSVARSH

#include "goby/moos/moos_header.h"

namespace goby
{
    namespace moos
    {
        class DynamicMOOSVars
        {
          public:
            const CMOOSMsg & get_moos_var(const std::string& s)
            { return vars[s]; }
                
            const CMOOSMsg & operator[](const std::string& s)
            { return vars[s]; }
                
            // read the whole list
            void update_moos_vars(const MOOSMSG_LIST& NewMail)
            {
                for(MOOSMSG_LIST::const_iterator p = NewMail.begin(), n = NewMail.end(); p != n; ++p)
                    vars[p->GetKey()] = *p;
            }
                
            // update a single variable at a time
            void update_moos_vars(const CMOOSMsg& msg)
            { vars[msg.GetKey()] = msg; }
                
            std::map<std::string, CMOOSMsg>& all() { return vars; }
                
          private:
            std::map<std::string, CMOOSMsg> vars;
                
        };
    }
}

inline bool valid(const CMOOSMsg& m)
{ return m.GetTime() != -1; }    
    
#endif
