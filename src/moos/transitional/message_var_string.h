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

#ifndef MESSAGE_VAR_STRING20100317H
#define MESSAGE_VAR_STRING20100317H

#include "goby/util/as.h"

#include "message_var.h"

namespace goby
{
    namespace transitional
    {   
        class DCCLMessageVarString : public DCCLMessageVar
        {
          public:
          DCCLMessageVarString()
              : DCCLMessageVar(),
                max_length_(0)
                { }

            void set_max_length(unsigned max_length) {max_length_ = max_length;}
            void set_max_length(const std::string& s) { set_max_length(util::as<unsigned>(s)); }

            unsigned max_length() const {return max_length_;}        

            DCCLType type() const  { return dccl_string; }
        
          private:
            void initialize_specific()
            { }
        
            void get_display_specific(std::stringstream& ss) const
            { }        

          private:
            unsigned max_length_;

        };
    }
}
#endif
