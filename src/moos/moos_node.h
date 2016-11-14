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

#ifndef MOOSNODE20110419H
#define MOOSNODE20110419H


#include "moos_serializer.h"
#include "moos_string.h"

#include "goby/common/node_interface.h"
#include "goby/common/zeromq_service.h"

namespace goby
{
    namespace moos
    {        
        class MOOSNode : public goby::common::NodeInterface<CMOOSMsg>
        {
          public:
            MOOSNode(goby::common::ZeroMQService* service);
            
            virtual ~MOOSNode()
            { }
            
            void send(const CMOOSMsg& msg, int socket_id, const std::string& group_unused = "");
            void subscribe(const std::string& full_or_partial_moos_name, int socket_id);
            void unsubscribe(const std::string& full_or_partial_moos_name, int socket_id);
            
            
            
            CMOOSMsg& newest(const std::string& key);

            // returns newest for "BOB", "BIG", when substr is "B"
            std::vector<CMOOSMsg> newest_substr(const std::string& substring);
            
            
          protected:
            // not const because CMOOSMsg requires mutable for many const calls...
            virtual void moos_inbox(CMOOSMsg& msg) = 0;
            
            
            
          private:
            void inbox(int marshalling_scheme,
                       const std::string& identifier,
                       const std::string& body,
                       int socket_id);

          private:
            std::map<std::string, boost::shared_ptr<CMOOSMsg> > newest_vars;
            
        };
    }
}


#endif
