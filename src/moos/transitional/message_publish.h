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

#ifndef PUBLISH20091211H
#define PUBLISH20091211H

#include <iostream>
#include <vector>
#include <sstream>

#include <boost/format.hpp>

#include "message_var.h"
#include "message_val.h"
#include "message_algorithms.h"
#include "dccl_constants.h"

namespace goby
{
    namespace transitional
    {
        class DCCLMessage;
    
// defines (a single) thing to do with the decoded message
// that is, where do we publish it and what should we include in the
// published message
        class DCCLPublish
        {
          public:
          DCCLPublish() : var_(""),
                format_(""),
                format_set_(false),
                use_all_names_(false),
                type_(cpp_notype),
                ap_(DCCLAlgorithmPerformer::getInstance()),
                repeat_(1)
                { }        

            //set
    
            void set_var(std::string var) {var_=var;}
            void set_format(std::string format) {format_=format; format_set_ = true;}
            void set_use_all_names(bool use_all_names) {use_all_names_ = use_all_names;}
            void set_type(DCCLCppType type) {type_ = type;}

            void add_name(const std::string& name) {names_.push_back(name);}
            void add_message_var(boost::shared_ptr<DCCLMessageVar> mv) {message_vars_.push_back(mv);}
            void add_algorithms(const std::vector<std::string> algorithms) {algorithms_.push_back(algorithms);}
    
            //get
            std::string var() const {return var_;}
            std::string format() const {return format_;}
            bool format_set()  const {return format_set_;}
            bool use_all_names() const {return use_all_names_;}
    
            DCCLCppType type() const {return type_;}
            std::vector<boost::shared_ptr<DCCLMessageVar> > const & message_vars() const { return message_vars_; }
            

            std::vector<std::string> const& names() const {return names_;}
            std::vector<std::vector<std::string> > const& algorithms() const {return algorithms_;}
    
        
        

            void initialize(const DCCLMessage& msg);
            
          private:
            std::string var_;
            std::string format_;
            bool format_set_;
            bool use_all_names_;
            DCCLCppType type_;
            std::vector<std::string> names_;
            std::vector<boost::shared_ptr<DCCLMessageVar> > message_vars_;
            std::vector< std::vector<std::string> > algorithms_;
            DCCLAlgorithmPerformer* ap_;
            unsigned repeat_;
        };
    }
}
#endif
