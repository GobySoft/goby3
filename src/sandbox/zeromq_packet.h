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

#ifndef ZeroMQPacket20160413H
#define ZeroMQPacket20160413H

#include <sstream>

namespace goby
{
    namespace sandbox
    {
        
        std::string zeromq_packet_make_header(const std::string& identifier)
        {
            std::string zmq_filter;
            zmq_filter += identifier + '\0';
            return zmq_filter;
        }
        
        /// \brief Encodes a packet for Goby over ZeroMQ 
        void zeromq_packet_encode(std::string* raw, const std::string& identifier, const std::string& body)
        {
            *raw = zeromq_packet_make_header(identifier);
            *raw += body;
        }

        /// \brief Decodes a packet for Goby over ZeroMQ
        void zeromq_packet_decode(const std::string& raw, std::string* identifier, std::string* body)
        {
            *identifier = raw.substr(0, raw.find('\0'));
            // +1 for null terminator
            const int HEADER_SIZE = identifier->size() + 1;
            *body = raw.substr(HEADER_SIZE);
        }
    }
}


#endif
