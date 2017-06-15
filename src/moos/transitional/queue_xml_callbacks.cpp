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

#include "queue_xml_callbacks.h"

using namespace xercesc;
using namespace goby::transitional::xml;

// this is called when the parser encounters the start tag, e.g. <message>
void goby::transitional::QueueContentHandler::startElement( 
    const XMLCh *const uri,        // namespace URI
    const XMLCh *const localname,  // tagname w/ out NS prefix
    const XMLCh *const qname,      // tagname + NS pefix
    const Attributes &attrs )      // elements's attributes
{
    //const XMLCh* val;
    Tag current_tag = tags_map_[toNative(localname)];
    parents_.insert(current_tag);
    
    switch(current_tag)
    {
        default:
            break;            
            
        case tag_message:
            q_.push_back(goby::transitional::protobuf::QueueConfig());
            q_.back().mutable_key()->set_type(goby::transitional::protobuf::QUEUE_DCCL);
            break;
    }
    
    current_text.clear();
}

void goby::transitional::QueueContentHandler::endElement(          
    const XMLCh *const uri,        // namespace URI
    const XMLCh *const localname,  // tagname w/ out NS prefix
    const XMLCh *const qname )     // tagname + NS pefix
{        
    std::string trimmed_data = boost::trim_copy(current_text);
    
    Tag current_tag = tags_map_[toNative(localname)];
    parents_.erase(current_tag);

    using goby::util::as;
    using google::protobuf::uint32;
    
    switch(current_tag)
    {            
        default:
            break;            

        case tag_ack:
            q_.back().set_ack(as<bool>(trimmed_data));
            break;            

        case tag_blackout_time:
            q_.back().set_blackout_time(as<uint32>(trimmed_data));
            break;

        case tag_max_queue:
            q_.back().set_max_queue(as<uint32>(trimmed_data));
            break;
            
        case tag_newest_first:
            q_.back().set_newest_first(as<bool>(trimmed_data));
            break;        

        case tag_id:
            q_.back().mutable_key()->set_id(as<uint32>(trimmed_data));
            break;

        case tag_priority_base:
        case tag_priority_time_const:
            // deprecated
            break;

        case tag_name:
            if(!in_message_var() && !in_header_var())
                q_.back().set_name(trimmed_data);
            break;
            

        case tag_ttl:
            q_.back().set_ttl(as<uint32>(trimmed_data));
            break;


        case tag_value_base:
            q_.back().set_value_base(as<double>(trimmed_data));
            break;

    }
}
