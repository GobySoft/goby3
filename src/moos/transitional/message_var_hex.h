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

#ifndef MESSAGE_VAR_HEX20100317H
#define MESSAGE_VAR_HEX20100317H

#include "message_var.h"
#include "goby/acomms/dccl.h"

#include "dccl_constants.h"
#include "goby/util/binary.h"
#include "goby/util/as.h"

namespace goby
{
    namespace transitional
    {   
        class DCCLMessageVarHex : public DCCLMessageVar
        {
          public:

          DCCLMessageVarHex()
              : DCCLMessageVar(),
                num_bytes_(0)
                { }

            /* int calc_size() const */
            /* { return num_bytes_*acomms::BITS_IN_BYTE; } */

            void set_num_bytes(unsigned num_bytes) {num_bytes_ = num_bytes;}
            void set_num_bytes(const std::string& s) { set_num_bytes(boost::lexical_cast<unsigned>(s)); }

            unsigned num_bytes() const {return num_bytes_;}        
            DCCLType type() const { return dccl_hex; }        
        
          private:
            void initialize_specific()
            { }
        
            void get_display_specific(std::stringstream& ss) const
            { }

          private:
            unsigned num_bytes_;

        };
    }
}
#endif
