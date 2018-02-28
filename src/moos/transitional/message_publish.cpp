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

#include <boost/foreach.hpp>

#include "message_publish.h"
#include "goby/acomms/dccl.h"
#include "message.h"



void goby::transitional::DCCLPublish::initialize(const DCCLMessage& msg)
{
    repeat_ = msg.repeat();

    // check and add all publish names grabbed by the xml parser
    BOOST_FOREACH(const std::string& name, names_)
        add_message_var(msg.name2message_var(name));


    BOOST_FOREACH(const std::vector<std::string>& algs, algorithms_)
    {
        BOOST_FOREACH(const std::string& alg, algs)
            ap_->check_algorithm(alg, msg);
    }
    
    
    // add names for any <all/> publishes and empty std::vector for algorithms
    if(use_all_names_)
    {
        BOOST_FOREACH(const boost::shared_ptr<DCCLMessageVar> mv, msg.header_const())
        {
            // ignore header pieces not explicitly overloaded by the <name> tag
            if(!mv->name().empty() && !(mv->name()[0] == '_') &&
               !std::count(names_.begin(), names_.end(), mv->name()))
            {
                add_message_var(mv);
                // add an empty std::vector for algorithms (no algorithms allowed for <all/> tag)
                add_algorithms(std::vector<std::string>());
            }
        }
        
        BOOST_FOREACH(const boost::shared_ptr<DCCLMessageVar> mv, msg.layout_const())
        {
            if(!std::count(names_.begin(), names_.end(), mv->name()))
            {
                add_message_var(mv);
                // add an empty std::vector for algorithms (no algorithms allowed for <all/> tag)
                add_algorithms(std::vector<std::string>());
            }
        }
    }
    
    int format_count = 0;
    // add format if publish doesn't have one  
    if(!format_set_)
    {
        std::string format_str;
        for (std::vector<boost::shared_ptr<DCCLMessageVar> >::size_type j = 0, m = message_vars_.size(); j < m; ++j)
        {
            if (m > 1)
            {
                if (j)
                    format_str += ",";
                
                // allows you to use the same message var twice but gives a unique name based on the algorithms used
                unsigned size = algorithms_[j].size();
                if(count(message_vars_.begin(), message_vars_.end(), message_vars_[j]) > 1 && size )
                {
                    for(unsigned i = 0; i < size; ++i)
                        format_str += algorithms_[j][i];
                            
                    format_str += "(" + message_vars_[j]->name() + ")=";
                }
                else
                    format_str += message_vars_[j]->name() + "=";           
            }

            for(unsigned i = 0,
                    n = (repeat_ > 1) ? 1 : message_vars_[j]->array_length();
                i < n;
                ++i)
            {                
                ++format_count;
                std::stringstream ss;

                if(m > 1 && n > 1 && i == 0) ss << "{";
                if(i) ss << ",";
                
                ss << "%" << format_count << "%";

                if(m > 1 && n > 1 && i+1 == n) ss << "}";
                format_str += ss.str();
            }            
        }
        format_ = format_str;
    } 
}


