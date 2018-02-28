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

#include "message_xml_callbacks.h"


#include "goby/util/as.h"

using namespace xercesc;
using namespace goby::transitional::xml;

// this is called when the parser encounters the start tag, e.g. <message>
void goby::transitional::DCCLMessageContentHandler::startElement( 
    const XMLCh *const uri,        // namespace URI
    const XMLCh *const localname,  // tagname w/ out NS prefix
    const XMLCh *const qname,      // tagname + NS pefix
    const Attributes &attrs )      // elements's attributes
{
    const XMLCh* val;

    // tag parameters
    static const XMLCh* algorithm = fromNative("algorithm");
    static const XMLCh* mandatory_content = fromNative("mandatory_content");
    static const XMLCh* key = fromNative("key");
    static const XMLCh* stype = fromNative("type");

    Tag current_tag = tags_map_[toNative(localname)];
    parents_.insert(current_tag);
    
    switch(current_tag)
    {
        default:
            break;

        case tag_message:
            // start new message
            messages.push_back(DCCLMessage());
            break;

        case tag_layout:
            break;

        case tag_publish:
            messages.back().add_publish();
            break;

        case tag_hex:
        case tag_int:
        case tag_string:
        case tag_float:
        case tag_bool:
        case tag_static:
        case tag_enum:
            messages.back().add_message_var(toNative(localname));
            if((val = attrs.getValue(algorithm)) != 0)
            {
                std::vector<std::string> vec;
                std::string str = boost::erase_all_copy(toNative(val), " ");
                boost::split(vec,
                             str,
                             boost::is_any_of(","));
                messages.back().last_message_var().set_algorithms(vec);
            }
            
            break;

            
        case tag_trigger_var:
            if((val = attrs.getValue(mandatory_content)) != 0)
                messages.back().set_trigger_mandatory(toNative(val));
            break;

            
        case tag_repeat:
            messages.back().set_repeat_enabled(true);
            break;
            
        case tag_all:
            messages.back().last_publish().set_use_all_names(true);
            break;

            
        case tag_src_var:
        case tag_publish_var:
        case tag_moos_var:
            if(in_message_var() && ((val = attrs.getValue(key)) != 0))
                messages.back().last_message_var().set_source_key(toNative(val));
            else if(in_header_var()  && ((val = attrs.getValue(key)) != 0))
                messages.back().header_var(curr_head_piece_).set_source_key(toNative(val));
            else if(in_publish() && ((val = attrs.getValue(stype)) != 0))
            {
                if(toNative(val) == "double")
                    messages.back().last_publish().set_type(cpp_double);
                else if(toNative(val) == "string")
                    messages.back().last_publish().set_type(cpp_string);
                else if(toNative(val) == "long")
                    messages.back().last_publish().set_type(cpp_long);
                else if(toNative(val) == "bool")
                    messages.back().last_publish().set_type(cpp_bool);
            }
            break;

        case tag_message_var:
            if(in_publish())
            {
                if((val = attrs.getValue(algorithm)) != 0)
                {
                    std::vector<std::string> vec;
                    std::string str = boost::erase_all_copy(toNative(val), " ");
                    boost::split(vec,
                                 str,
                                 boost::is_any_of(","));
                    messages.back().last_publish().add_algorithms(vec);
                }
                else
                {
                    messages.back().last_publish().add_algorithms(std::vector<std::string>());
                }
            }
            break;

        case tag_time:
            curr_head_piece_ = HEAD_TIME;
            if((val = attrs.getValue(algorithm)) != 0)
            {
                std::vector<std::string> vec;
                std::string str = boost::erase_all_copy(toNative(val), " ");
                boost::split(vec,
                             str,
                             boost::is_any_of(","));

                messages.back().header_var(curr_head_piece_).set_algorithms(vec);
            }
            break;

        case tag_src_id:
            curr_head_piece_ = HEAD_SRC_ID;
            if((val = attrs.getValue(algorithm)) != 0)
            {
                std::vector<std::string> vec;
                std::string str = boost::erase_all_copy(toNative(val), " ");
                boost::split(vec,
                             str,
                             boost::is_any_of(","));
                messages.back().header_var(curr_head_piece_).set_algorithms(vec);
            }
            
            break;

        case tag_dest_id:
            curr_head_piece_ = HEAD_DEST_ID;
            if((val = attrs.getValue(algorithm)) != 0)
            {
                std::vector<std::string> vec;
                std::string str = boost::erase_all_copy(toNative(val), " ");
                boost::split(vec,
                             str,
                             boost::is_any_of(","));
                messages.back().header_var(curr_head_piece_).set_algorithms(vec);
            }
            break;

            // legacy
        case tag_destination_var:
            if((val = attrs.getValue(key)) != 0)
                messages.back().header_var(HEAD_DEST_ID).set_name(toNative(val));
            break;            
    }

    current_text.clear(); // starting a new tag, clear any old CDATA
}

void goby::transitional::DCCLMessageContentHandler::endElement(          
    const XMLCh *const uri,        // namespace URI
    const XMLCh *const localname,  // tagname w/ out NS prefix
    const XMLCh *const qname )     // tagname + NS pefix
{        
    std::string trimmed_data = boost::trim_copy(current_text);

    Tag current_tag = tags_map_[toNative(localname)];
    parents_.erase(current_tag);
    
    switch(current_tag)
    {
        case tag_message:
            messages.back().preprocess(); // do any message tasks that require all data
            break;
            
        case tag_layout:
            break;
                
        case tag_publish:
            break;                

        case tag_hex:
        case tag_int:
        case tag_bool:
        case tag_enum:
        case tag_float:
        case tag_static:
        case tag_string:
            break;

        case tag_max_delta:
            messages.back().last_message_var().set_max_delta(trimmed_data);
            break;
            
        case tag_format:
            messages.back().last_publish().set_format(trimmed_data);
            break;
            
        case tag_id:
            messages.back().set_id(trimmed_data);
            break;
            
        case tag_incoming_hex_moos_var:
            messages.back().set_in_var(trimmed_data);
            break;
                
        case tag_max_length:
            messages.back().last_message_var().set_max_length(trimmed_data);
            break;

        case tag_num_bytes:
            messages.back().last_message_var().set_num_bytes(trimmed_data);
            break;
            
        case tag_max:
            messages.back().last_message_var().set_max(trimmed_data);
            break;
            
        case tag_message_var:
            messages.back().last_publish().add_name(trimmed_data);  
            break;            

        case tag_min:
            messages.back().last_message_var().set_min(trimmed_data);
            break;
            
        case tag_src_var:
        case tag_publish_var:
        case tag_moos_var:
            if(in_message_var())
                messages.back().last_message_var().set_source_var(trimmed_data);
            else if(in_header_var())
                messages.back().header_var(curr_head_piece_).set_source_var(trimmed_data);
            else if(in_publish())
                messages.back().last_publish().set_var(trimmed_data);
            break;

            // legacy
        case tag_destination_var:
            messages.back().header_var(HEAD_DEST_ID).set_source_var(trimmed_data);
            break;
            
            
        case tag_name:
            if(!in_message_var() && !in_header_var())
                messages.back().set_name(trimmed_data);
            else if(in_message_var())
                messages.back().last_message_var().set_name(trimmed_data);
            else if(in_header_var())
                messages.back().header_var(curr_head_piece_).set_name(trimmed_data);
            break;
            
            
        case tag_outgoing_hex_moos_var:
            messages.back().set_out_var(trimmed_data);
            break;
            
        case tag_precision:
            messages.back().last_message_var().set_precision(trimmed_data);
            break;
            
            
        case tag_size:
            messages.back().set_size(trimmed_data);
            break;

        case tag_trigger_var:
            messages.back().set_trigger_var(trimmed_data);        
            break;
            
        case tag_trigger_time:
            messages.back().set_trigger_time(trimmed_data);
            break;
        
        case tag_trigger:
            messages.back().set_trigger(trimmed_data);
            break;
            
        case tag_value:
            if(parents_.count(tag_static))
                messages.back().last_message_var().set_static_val(trimmed_data);
            else if(parents_.count(tag_enum))
                messages.back().last_message_var().add_enum(trimmed_data);
            break;

        case tag_array_length:
            messages.back().last_message_var().set_array_length(trimmed_data);
            break;

            
        default:
            break;
            
    }
}
