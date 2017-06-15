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

// name of Class, initial date (when file was first created) in YYYYMMDD, followed by capital 'H'
#ifndef Class20080605H
#define Class20080605H

// std
#include <string>

// third party
#include <boost/algorithm/string.hpp>

// local to goby
#include "acomms/modem_message.h"

// local to folder (part of same application / library)
#include "support_class.h"

// typedef
typedef std::list<micromodem::Message>::iterator messages_it;

// incomplete declarations: use whenever possible!
class FlexCout;

namespace nspace
{
    class Class
    {
      public:
        // "FlexCout* os", not "FlexCout *os:
        // in C++ we emphasize objects
        // (read: "os" is a FlexCout pointer)
        Class(FlexCout* os = NULL,
              unsigned int modem_id = 0,
              bool is_ccl = false);
        
        // definitions
        bool push_message(micromodem::Message& new_message);
            
        // inlines
        void clear_ack_queue() { waiting_for_ack_.clear(); }
        
        // only if class is intended to be derived
      protected: //methods    
        
      private: // methods
        messages_it next_message_it();    
        
      private: // data, no public data
        std::string name_;
        FlexCout* os_;
    };

    // related non class functions
    std::ostream & operator<< (std::ostream& os, const Class& oq);
}

#endif
