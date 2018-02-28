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

#include "goby/util/as.h"
#include "goby/moos/moos_string.h"

#include "message_var.h"
#include "message.h"
#include "message_val.h"
#include "message_algorithms.h"

goby::transitional::DCCLMessageVar::DCCLMessageVar()
    : array_length_(1),
      is_key_frame_(true),
      sequence_number_(-1),
      source_set_(false),
      ap_(DCCLAlgorithmPerformer::getInstance())
{ }

void goby::transitional::DCCLMessageVar::initialize(const DCCLMessage& msg)
{
    // add trigger_var_ as source_var for any message_vars without a source
    if(!source_set_)
        source_var_ = msg.trigger_var();

    BOOST_FOREACH(const std::string& alg, algorithms_)
        ap_->check_algorithm(alg, msg);
    
    initialize_specific();

}

void goby::transitional::DCCLMessageVar::set_defaults(std::map<std::string,std::vector<DCCLMessageVal> >& vals, unsigned modem_id, unsigned id)
{
    vals[name_].resize(array_length_);    

    std::vector<DCCLMessageVal>& vm = vals[name_];

    for(std::vector<DCCLMessageVal>::size_type i = 0, n = vm.size(); i < n; ++i)
        set_defaults_specific(vm[i], modem_id, id);

}




void goby::transitional::DCCLMessageVar::write_schema_to_dccl2(std::ofstream* proto_file, int sequence_number)
{
    sequence_number_ = sequence_number;

    
    *proto_file << "\t";

    if(array_length_ == 1)
        *proto_file << "optional ";
    else
        *proto_file  << "repeated ";

    std::string enum_name;
    if(type() == dccl_enum)
    {
        enum_name = name_ + "Enum";
        enum_name[0] = toupper(enum_name[0]);
        *proto_file << enum_name;
    }
    else
    {
        *proto_file << type_to_protobuf_type(type());
    }

    *proto_file << " " << name_ << " = " << sequence_number;


    int count = 0;
    try
    {
        double max_tmp = max();

        *proto_file << (count ? ", " : " [");
        ++count;
        *proto_file << "(dccl.field).max=" << goby::util::as<std::string>(max_tmp, precision(), goby::util::FLOAT_FIXED);
    }
    catch(...) { }
    try
    {
        double min_tmp = min();
        *proto_file << (count ? ", " : " [");
        ++count;
        *proto_file << "(dccl.field).min=" << goby::util::as<std::string>(min_tmp, precision(), goby::util::FLOAT_FIXED);
    }
    catch(...) { }
    try
    {
        if(type() == dccl_float)
        {
            int precision_tmp = precision();
            *proto_file << (count ? ", " : " [");
            ++count;
            *proto_file << "(dccl.field).precision=" << precision_tmp;
        }
    }
    catch(...) { }
    try
    {
        unsigned max_length_tmp = max_length();
        *proto_file << (count ? ", " : " [");
        ++count;
        *proto_file << "(dccl.field).max_length=" << max_length_tmp;        
    }
    catch(...) { }
    try
    {
        unsigned num_bytes_tmp = num_bytes();
        *proto_file << (count ? ", " : " [");
        ++count;
        *proto_file << "(dccl.field).max_length=" << num_bytes_tmp;        
    }
    catch(...) { }
    try
    {
        std::string static_val_tmp = static_val();
        *proto_file << (count ? ", " : " [");
        ++count;
        *proto_file << "default=\"" << static_val_tmp << "\", (dccl.field).static_value=\"" << static_val_tmp << "\", " << "(dccl.field).codec=\"_static\"";      
    }
    catch(...) { }

    if(array_length_ != 1)
    {
        *proto_file << (count ? ", " : " [");
        ++count;
        *proto_file << "(dccl.field).max_repeat=" << array_length_;        

    }

    
    std::string more_options = additional_option_extensions();
    if(!more_options.empty())
    {
        *proto_file << (count ? ", " : " [");
        ++count;
        *proto_file << more_options;
    }
    
    
 
    if(count)
        *proto_file << "]";

    *proto_file << ";" <<  std::endl;

    if(type() == dccl_enum)
    {
        *proto_file << "\t" << "enum " << enum_name << "{ \n";

        int enum_value = 0;
        BOOST_FOREACH(const std::string& e, *enums())
            *proto_file << "\t\t " << boost::to_upper_copy(name_) << "_" << e << " = " << enum_value++ << "; \n";

        *proto_file << "\t} \n";
    }
}

