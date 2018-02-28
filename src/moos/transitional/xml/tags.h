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

#ifndef XMLTags20091211H
#define XMLTags20091211H

#include "goby/moos/transitional/xml/xerces_strings.h"

#include <boost/assign.hpp>

namespace goby
{
    namespace transitional
    {    
        namespace xml
        {
            
            enum Tag { tag_not_defined,
                       tag_all,
                       tag_array_length,
                       tag_bool,
                       tag_delta_encode,
                       tag_enum,
                       tag_max_delta,
                       tag_float,
                       tag_format,
                       tag_hex,
                       tag_id,
                       tag_incoming_hex_moos_var,
                       tag_int,
                       tag_key,
                       tag_layout,
                       tag_max_length,
                       tag_max,
                       tag_message_var,
                       tag_message,
                       tag_min,
                       tag_moos_var,
                       tag_name,
                       tag_num_bytes,
                       tag_outgoing_hex_moos_var,
                       tag_precision, 
                       tag_publish,
                       tag_publish_var,
                       tag_repeat,
                       tag_size,
                       tag_source,
                       tag_src_var,
                       tag_static,
                       tag_string,
                       tag_trigger_var,
                       tag_trigger_time,
                       tag_trigger,
                       tag_value,
                       tag_time,
                       tag_src_id,
                       tag_dest_id,
                       tag_destination_var,
                       tag_ack,
                       tag_blackout_time,
                       tag_max_queue,
                       tag_newest_first,
                       tag_priority_base,
                       tag_priority_time_const,
                       tag_ttl,
                       tag_value_base
            };

    
            static void initialize_tags(std::map<std::string, Tag>& tags_map)
            {
                boost::assign::insert(tags_map)
                    ("all",tag_all)
                    ("array_length",tag_array_length)
                    ("bool",tag_bool)
                    ("delta_encode", tag_delta_encode)
                    ("enum",tag_enum)
                    ("max_delta",tag_max_delta)
                    ("float",tag_float)
                    ("format",tag_format)
                    ("hex", tag_hex)
                    ("id",tag_id)
                    ("incoming_hex_moos_var",tag_incoming_hex_moos_var)
                    ("int",tag_int)
                    ("key",tag_key)
                    ("layout",tag_layout)
                    ("num_bytes",tag_num_bytes)
                    ("max_length",tag_max_length)
                    ("max",tag_max)
                    ("message_var",tag_message_var)
                    ("message",tag_message)
                    ("min",tag_min)
                    ("moos_var",tag_moos_var)
                    ("name",tag_name)
                    ("outgoing_hex_moos_var",tag_outgoing_hex_moos_var)
                    ("precision", tag_precision) 
                    ("publish",tag_publish)
                    ("publish_var",tag_publish_var)
                    ("repeat",tag_repeat)
                    ("size",tag_size)
                    ("source",tag_source)
                    ("src_var",tag_src_var)
                    ("static",tag_static)
                    ("string",tag_string)
                    ("trigger_moos_var",tag_trigger_var)
                    ("trigger_var",tag_trigger_var)
                    ("trigger_time",tag_trigger_time)
                    ("trigger",tag_trigger)
                    ("value",tag_value)
                    ("time",tag_time)
                    ("src_id",tag_src_id)
                    ("dest_id",tag_dest_id)
                    ("destination_var",tag_destination_var)
                    ("destination_moos_var",tag_destination_var)
                    ("ack",tag_ack)
                    ("blackout_time",tag_blackout_time)
                    ("max_queue",tag_max_queue)
                    ("newest_first",tag_newest_first)
                    ("priority_base",tag_priority_base)
                    ("priority_time_const",tag_priority_time_const)
                    ("ttl",tag_ttl)
                    ("value_base",tag_value_base);

            }
    
            inline bool in_message_var(std::set<Tag>& parents)
            {
                if(parents.count(tag_hex)) return true;
                else if(parents.count(tag_int)) return true;
                else if(parents.count(tag_string)) return true;
                else if(parents.count(tag_float)) return true;
                else if(parents.count(tag_bool)) return true;
                else if(parents.count(tag_static)) return true;
                else if(parents.count(tag_enum)) return true;
                else return false;
            }
            inline bool in_header_var(std::set<Tag>& parents)
            {
                if(parents.count(tag_time)) return true;
                else if(parents.count(tag_src_id)) return true;
                else if(parents.count(tag_dest_id)) return true;
                else return false;
            }
    
            inline bool in_publish(std::set<Tag>& parents)
            { return parents.count(tag_publish); }
        }    
    }
}

#endif
