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
#include "goby/common/logger.h"
#include "message.h"
#include "goby/acomms/dccl/dccl.h"


using goby::util::as;
using goby::glog;


goby::transitional::DCCLMessage::DCCLMessage():size_(0),
                         trigger_number_(1),
                         id_(0),
                         trigger_time_(0.0),
                         repeat_enabled_(false),
                         repeat_(1)
{
    header_.resize(DCCL_NUM_HEADER_PARTS);
    header_[HEAD_CCL_ID] =
        boost::shared_ptr<DCCLMessageVar>(new DCCLMessageVarCCLID());
    header_[HEAD_DCCL_ID] =
        boost::shared_ptr<DCCLMessageVar>(new DCCLMessageVarDCCLID());
    header_[HEAD_TIME] =
        boost::shared_ptr<DCCLMessageVar>(new DCCLMessageVarTime());
    header_[HEAD_SRC_ID] =
        boost::shared_ptr<DCCLMessageVar>(new DCCLMessageVarSrc());
    header_[HEAD_DEST_ID] =
        boost::shared_ptr<DCCLMessageVar>(new DCCLMessageVarDest());
    header_[HEAD_MULTIMESSAGE_FLAG] =
        boost::shared_ptr<DCCLMessageVar>(new DCCLMessageVarMultiMessageFlag());
    header_[HEAD_BROADCAST_FLAG] =
        boost::shared_ptr<DCCLMessageVar>(new DCCLMessageVarBroadcastFlag());
    header_[HEAD_UNUSED] =
        boost::shared_ptr<DCCLMessageVar>(new DCCLMessageVarUnused());
}

    
// add a new message_var to the current messages vector
void goby::transitional::DCCLMessage::add_message_var(const std::string& type)
{
    if(type == "static")
        layout_.push_back(boost::shared_ptr<DCCLMessageVar>(new DCCLMessageVarStatic()));
    else if(type == "int")
        layout_.push_back(boost::shared_ptr<DCCLMessageVar>(new DCCLMessageVarInt()));
    else if(type == "string")
        layout_.push_back(boost::shared_ptr<DCCLMessageVar>(new DCCLMessageVarString()));
    else if(type == "float")
        layout_.push_back(boost::shared_ptr<DCCLMessageVar>(new DCCLMessageVarFloat()));
    else if(type == "enum")
        layout_.push_back(boost::shared_ptr<DCCLMessageVar>(new DCCLMessageVarEnum()));
    else if(type == "bool")
        layout_.push_back(boost::shared_ptr<DCCLMessageVar>(new DCCLMessageVarBool()));
    else if(type == "hex")
        layout_.push_back(boost::shared_ptr<DCCLMessageVar>(new DCCLMessageVarHex()));
}

// add a new publish, i.e. a set of parameters to publish
// upon receipt of an incoming (hex) message
void goby::transitional::DCCLMessage::add_publish()
{
    DCCLPublish p;
    publishes_.push_back(p);
}

// a number of tasks to perform after reading in an entire <message> from
// the xml file
void goby::transitional::DCCLMessage::preprocess()
{
    // calculate number of repeated messages that will fit and put this in `repeat_`.
    if(repeat_enabled_)
    {
        glog.is(goby::common::logger::WARN) && glog << "<repeat> is deprecated and ignored in this version of Goby. Simply post a message several times." << std::endl;
    }

//    body_bits_ = calc_total_size();

    // initialize header vars
    BOOST_FOREACH(boost::shared_ptr<DCCLMessageVar> mv, header_)
        mv->initialize(*this);
    BOOST_FOREACH(boost::shared_ptr<DCCLMessageVar> mv, layout_)
        mv->initialize(*this);
    
    // iterate over publishes_
    BOOST_FOREACH(DCCLPublish& p, publishes_)
        p.initialize(*this);
    
    // set incoming_var / outgoing_var if not set
    if(in_var_ == "")
        in_var_ = "IN_" + boost::to_upper_copy(name_) + "_HEX_" + as<std::string>(size_) + "B";
    if(out_var_ == "")
        out_var_ = "OUT_" + boost::to_upper_copy(name_) + "_HEX_" + as<std::string>(size_) + "B";

    
}

void goby::transitional::DCCLMessage::set_repeat_array_length()
{
    // set array_length_ for repeated messages for all DCCLMessageVars
    BOOST_FOREACH(boost::shared_ptr<DCCLMessageVar> mv, layout_)
        mv->set_array_length(repeat_);
}

unsigned goby::transitional::DCCLMessage::calc_total_size()
{
    boost::shared_ptr<acomms::DCCLFieldCodecBase> codec =
        acomms::DCCLFieldCodecManager::find(descriptor_, descriptor_->options().GetExtension(dccl::msg).codec());
    unsigned u = 0;
    codec->base_max_size(&u, descriptor_, acomms::MessageHandler::BODY);
    return u;
}


std::map<std::string, std::string> goby::transitional::DCCLMessage::message_var_names() const
{
    std::map<std::string, std::string> s;
    BOOST_FOREACH(const boost::shared_ptr<DCCLMessageVar> mv, layout_)
        s.insert(std::pair<std::string, std::string>(mv->name(), type_to_string(mv->type())));
    return s;
}

    

void goby::transitional::DCCLMessage::set_head_defaults(std::map<std::string, std::vector<DCCLMessageVal> >& in, unsigned modem_id)
{
    for (std::vector< boost::shared_ptr<DCCLMessageVar> >::iterator it = header_.begin(),
             n = header_.end();
         it != n;
         ++it)
    {
        (*it)->set_defaults(in, modem_id, id_);
    }
}

boost::shared_ptr<goby::transitional::DCCLMessageVar> goby::transitional::DCCLMessage::name2message_var(const std::string& name) const 
{
    BOOST_FOREACH(boost::shared_ptr<DCCLMessageVar> mv, layout_)
    {
        if(mv->name() == name) return mv;
    }    
    BOOST_FOREACH(boost::shared_ptr<DCCLMessageVar> mv, header_)
    {
        if(mv->name() == name) return mv;
    }

    throw goby::acomms::DCCLException(std::string("DCCL: no such name \"" + name + "\" found in <layout> or <header>"));
    
    return boost::shared_ptr<DCCLMessageVar>();
}

// Added in Goby2 for transition to Protobuf structure
void goby::transitional::DCCLMessage::write_schema_to_dccl2(std::ofstream* proto_file)
{
    
    *proto_file << "message " << name_ << " { " << std::endl;
    *proto_file << "\t" << "option (dccl.msg).id = " << id_ << ";" << std::endl;
    *proto_file << "\t" << "option (dccl.msg).max_bytes = " << size_ << ";" << std::endl;
    
    int sequence_number = 0;
    
    
    
    header_[HEAD_TIME]->write_schema_to_dccl2(proto_file, ++sequence_number);
    header_[HEAD_SRC_ID]->write_schema_to_dccl2(proto_file, ++sequence_number);
    header_[HEAD_DEST_ID]->write_schema_to_dccl2(proto_file, ++sequence_number);
    
    BOOST_FOREACH(boost::shared_ptr<DCCLMessageVar> mv, layout_)
        mv->write_schema_to_dccl2(proto_file, ++sequence_number);
    
    *proto_file << "} " << std::endl;
}

