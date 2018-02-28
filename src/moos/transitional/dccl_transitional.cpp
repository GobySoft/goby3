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
#include <boost/filesystem.hpp>

#include "dccl_transitional.h"
#include "message_xml_callbacks.h"
#include "queue_xml_callbacks.h"
#include "goby/common/logger.h"
#include "goby/util/as.h"
#include <google/protobuf/descriptor.pb.h>
#include "goby/util/dynamic_protobuf_manager.h"
#include <boost/regex.hpp>

using goby::common::goby_time;
using goby::util::as;           
using goby::acomms::operator<<;
using namespace goby::common::logger;


/////////////////////
// public methods (general use)
/////////////////////
goby::transitional::DCCLTransitionalCodec::DCCLTransitionalCodec()
    : log_(0),
      dccl_(goby::acomms::DCCLCodec::get()),
      start_time_(goby_time())
{
    goby::util::DynamicProtobufManager::enable_compilation();
}

void goby::transitional::DCCLTransitionalCodec::convert_to_v2_representation(pAcommsHandlerConfig* cfg)
{
    cfg_ = cfg->transitional_cfg();
    
    for(int i = 0, n = cfg->transitional_cfg().message_file_size(); i < n; ++i)
    {
        convert_xml_message_file(cfg->transitional_cfg().message_file(i),
                                 cfg->add_load_proto_file(),
                                 cfg->mutable_translator_entry(),
                                 cfg->mutable_queue_cfg());
    }
}


void goby::transitional::DCCLTransitionalCodec::convert_xml_message_file(
    const goby::transitional::protobuf::MessageFile& message_file,
    std::string* proto_file,
    google::protobuf::RepeatedPtrField<goby::moos::protobuf::TranslatorEntry>* translator_entries,
    goby::acomms::protobuf::QueueManagerConfig* queue_cfg)
{    
    const std::string& xml_file = message_file.path();

    size_t begin_size = messages_.size();            
        
    // Register handlers for XML parsing
    DCCLMessageContentHandler message_content(messages_);
    DCCLMessageErrorHandler message_error;
    // instantiate a parser for the xml message files
    XMLParser message_parser(message_content, message_error);

    std::vector<goby::transitional::protobuf::QueueConfig> old_queue_cfg;
    QueueContentHandler queue_content(old_queue_cfg);
    QueueErrorHandler queue_error;
    // instantiate a parser for the xml message files
    XMLParser queue_parser(queue_content, queue_error);
    
    message_parser.parse(xml_file);
    queue_parser.parse(xml_file);
    
    size_t end_size = messages_.size();
    
    check_duplicates();

    std::vector<unsigned> added_ids;
    std::set<unsigned> set_added_ids;

    boost::filesystem::path xml_file_path(xml_file);

    std::string generated_proto_dir = cfg_.generated_proto_dir();
    if(!generated_proto_dir.empty() && generated_proto_dir[generated_proto_dir.size() - 1] != '/')
        generated_proto_dir += "/";

#if BOOST_FILESYSTEM_VERSION == 3
    std::string xml_path_stem  = xml_file_path.stem().string();
    boost::filesystem::path proto_file_path = boost::filesystem::absolute( generated_proto_dir + xml_path_stem + ".proto");
#else
    std::string xml_path_stem  = xml_file_path.stem();
    boost::filesystem::path proto_file_path = boost::filesystem::complete( generated_proto_dir + xml_path_stem + ".proto");
#endif

    proto_file_path.normalize();
    
    for(size_t i = 0, n = end_size - begin_size; i < n; ++i)
    {
        // map name/id to position in messages_ vector for later use
        size_t new_index = messages_.size()-(n-i);
        name2messages_.insert(std::pair<std::string, size_t>(messages_[new_index].name(), new_index));
        id2messages_.insert(std::pair<unsigned, size_t>(messages_[new_index].id(), new_index));
 
        set_added_ids.insert(messages_[new_index].id());
        added_ids.push_back(messages_[new_index].id());
    }

    *proto_file = proto_file_path.string();

    

    std::ofstream fout(proto_file->c_str(), std::ios::out);
    
    if(!fout.is_open())
        throw(goby::acomms::DCCLException("Could not open " + *proto_file + " for writing"));

    fout << "import \"dccl/protobuf/option_extensions.proto\";" << std::endl;
    fout << "import \"goby/common/protobuf/option_extensions.proto\";" << std::endl;

    for(int i = 0, n = added_ids.size(); i < n; ++i)
    {
        to_iterator(added_ids[i])->write_schema_to_dccl2(&fout);
        
        goby::acomms::protobuf::QueuedMessageEntry* queue_entry = queue_cfg->add_message_entry();
        queue_entry->set_protobuf_name(to_iterator(added_ids[i])->name());
        if(old_queue_cfg[i].has_ack())
            queue_entry->set_ack(old_queue_cfg[i].ack());
        if(old_queue_cfg[i].has_blackout_time())
            queue_entry->set_blackout_time(old_queue_cfg[i].blackout_time());
        if(old_queue_cfg[i].has_max_queue())
            queue_entry->set_max_queue(old_queue_cfg[i].max_queue());
        if(old_queue_cfg[i].has_newest_first())
            queue_entry->set_newest_first(old_queue_cfg[i].newest_first());
        if(old_queue_cfg[i].has_value_base())
            queue_entry->set_value_base(old_queue_cfg[i].value_base());
        if(old_queue_cfg[i].has_ttl())
            queue_entry->set_ttl(old_queue_cfg[i].ttl());        
        
        goby::acomms::protobuf::QueuedMessageEntry::Role* src_role = queue_entry->add_role();
        src_role->set_type(goby::acomms::protobuf::QueuedMessageEntry::SOURCE_ID);
        src_role->set_field(to_iterator(added_ids[i])->header_var(HEAD_SRC_ID).name());
        
        goby::acomms::protobuf::QueuedMessageEntry::Role* dest_role = queue_entry->add_role();
        dest_role->set_type(goby::acomms::protobuf::QueuedMessageEntry::DESTINATION_ID);
        dest_role->set_field(to_iterator(added_ids[i])->header_var(HEAD_DEST_ID).name());    
        
        goby::acomms::protobuf::QueuedMessageEntry::Role* time_role = queue_entry->add_role();
        time_role->set_type(goby::acomms::protobuf::QueuedMessageEntry::TIMESTAMP);
        time_role->set_field(to_iterator(added_ids[i])->header_var(HEAD_TIME).name());
    
        for(int i = 0, n = message_file.manipulator_size(); i < n; ++i)
            queue_entry->add_manipulator(message_file.manipulator(i));
    }
    
    fout.close();
    
    const google::protobuf::FileDescriptor* file_desc =
        goby::util::DynamicProtobufManager::user_descriptor_pool().FindFileByName(*proto_file);    

    if(file_desc)
    {
        glog.is(DEBUG2) && glog << file_desc->DebugString() << std::flush;

        BOOST_FOREACH(unsigned id, added_ids)
        {
            const google::protobuf::Descriptor* desc = file_desc->FindMessageTypeByName(to_iterator(id)->name());

            if(desc)
                dccl_->validate(desc);
            else
            {
                glog << die << "No descriptor with name " << to_iterator(id)->name() << " found!" << std::endl;
            }

            to_iterator(id)->set_descriptor(desc);
        }
    }
    else
    {
        glog << die << "No file_descriptor with name " << *proto_file << " found!" << std::endl;
    }
    
    

    BOOST_FOREACH(unsigned id, added_ids)
    {
        
        goby::moos::protobuf::TranslatorEntry* entry = translator_entries->Add();
        entry->set_use_short_enum(true);
        std::vector<DCCLMessage>::const_iterator msg_it = to_iterator(id);
        entry->set_protobuf_name(msg_it->name());

        if(msg_it->trigger_type() == "time")
        {
            entry->mutable_trigger()->set_type(goby::moos::protobuf::TranslatorEntry::Trigger::TRIGGER_TIME);
            entry->mutable_trigger()->set_period(msg_it->trigger_time());
        }
        else if(msg_it->trigger_type() == "publish")
        {
            entry->mutable_trigger()->set_type(goby::moos::protobuf::TranslatorEntry::Trigger::TRIGGER_PUBLISH);
            entry->mutable_trigger()->set_moos_var(msg_it->trigger_var());
            if(!msg_it->trigger_mandatory().empty())
                entry->mutable_trigger()->set_mandatory_content(msg_it->trigger_mandatory());
            
        }
        
        std::map<std::string, goby::moos::protobuf::TranslatorEntry::CreateParser*> parser_map;
        std::map<std::string, int > sequence_map;
        int max_sequence = 0;
        BOOST_FOREACH(boost::shared_ptr<DCCLMessageVar> var, msg_it->header_const())
        {
            sequence_map[var->name()] = var->sequence_number();
            max_sequence = (var->sequence_number() > max_sequence) ? var->sequence_number() : max_sequence;
            
            fill_create(var, &parser_map, entry);
        }
        BOOST_FOREACH( boost::shared_ptr<DCCLMessageVar> var, msg_it->layout_const())
        {
            sequence_map[var->name()] = var->sequence_number();
            max_sequence = (var->sequence_number() > max_sequence) ? var->sequence_number() : max_sequence;
            
            fill_create(var, &parser_map, entry);
        }

        max_sequence = ((max_sequence + 100)/100)*100;
        
        for(int i = 0, n = msg_it->publishes_const().size(); i < n; ++i)
        {
            const DCCLPublish& publish = msg_it->publishes_const()[i];    

            goby::moos::protobuf::TranslatorEntry::PublishSerializer* serializer = entry->add_publish();
           serializer->set_technique(goby::moos::protobuf::TranslatorEntry::TECHNIQUE_FORMAT);

           // renumber format numbers
           boost::regex pattern ("%([0-9]+)",boost::regex_constants::icase|boost::regex_constants::perl);
           std::string replace ("%_GOBY1TEMP_\\1_");

           std::string new_moos_var = boost::regex_replace(publish.var(), pattern, replace);


           std::string old_format = publish.format();
           std::string new_format = boost::regex_replace(old_format, pattern, replace);

           int v1_index = 1;
           for(int j = 0, m = publish.message_vars().size(); j < m; ++j)
           {
               for(int k = 0, o = publish.message_vars()[j]->array_length(); k < o; ++k)
               {

                   int replacement_field = sequence_map[publish.message_vars()[j]->name()];
                   
                   // add any algorithms
                   bool added_algorithms = false;
                   BOOST_FOREACH(const std::string& algorithm, publish.algorithms()[j])
                   {
                       added_algorithms = true;
                       goby::moos::protobuf::TranslatorEntry::PublishSerializer::Algorithm * new_alg = serializer->add_algorithm();
                       std::vector<std::string> algorithm_parts;
                       boost::split(algorithm_parts, algorithm, boost::is_any_of(":"));
                       
                       new_alg->set_name(algorithm_parts[0]);
                       new_alg->set_primary_field(sequence_map[publish.message_vars()[j]->name()]);
                       
                       for(int l = 1, p = algorithm_parts.size(); l < p; ++l)
                           new_alg->add_reference_field(sequence_map[algorithm_parts[l]]);
                       
                       new_alg->set_output_virtual_field(max_sequence);
                   }
                   
                   if(added_algorithms)
                   {
                       replacement_field = max_sequence;
                       ++max_sequence;
                   }

                   std::string v2_index = goby::util::as<std::string>(replacement_field);
                   if(o > 1)
                       v2_index += "." + goby::util::as<std::string>(k);
                   
                   boost::algorithm::replace_all(new_moos_var,
                                                 "%_GOBY1TEMP_" + goby::util::as<std::string>(v1_index) + "_",
                                                 "%" + v2_index);
                   boost::algorithm::replace_all(new_format,
                                                 "%_GOBY1TEMP_" + goby::util::as<std::string>(v1_index) + "_",
                                                 "%" + v2_index);

                   ++v1_index;
               }
           }
           serializer->set_moos_var(new_moos_var);
           serializer->set_format(new_format);
           
    
           
        }

    }

    
        
}

void goby::transitional::DCCLTransitionalCodec::fill_create(boost::shared_ptr<DCCLMessageVar> var,
                                                            std::map<std::string, goby::moos::protobuf::TranslatorEntry::CreateParser*>* parser_map,
                                                            goby::moos::protobuf::TranslatorEntry* entry)
{
 
    // entry already exists for this type
    if((*parser_map).count(var->source_var()))
    {
        // use key=value parsing
        (*parser_map)[var->source_var()]->set_technique(goby::moos::protobuf::TranslatorEntry::TECHNIQUE_COMMA_SEPARATED_KEY_EQUALS_VALUE_PAIRS);
        (*parser_map)[var->source_var()]->clear_format();
                
 
                
    }
    else if(!var->source_var().empty())
    {
        (*parser_map)[var->source_var()] = entry->add_create();
        (*parser_map)[var->source_var()]->set_format("%" + goby::util::as<std::string>(var->sequence_number()) + "%");
        (*parser_map)[var->source_var()]->set_technique(goby::moos::protobuf::TranslatorEntry::TECHNIQUE_FORMAT);
        (*parser_map)[var->source_var()]->set_moos_var(var->source_var());
    }

    // add any algorithms
    BOOST_FOREACH(const std::string& algorithm, var->algorithms())
    {
        if(var->source_var().empty())
            throw(std::runtime_error("Algorithms without a corresponding source variable are not permitted in Goby v2 --> v1 backwards compatibility as the MOOS Translator does all algorithm conversions in Goby2"));

        
        goby::moos::protobuf::TranslatorEntry::CreateParser::Algorithm * new_alg =
            (*parser_map)[var->source_var()]->add_algorithm();

        if(algorithm.find(':') != std::string::npos)
        {
            throw(std::runtime_error("Algorithms with reference fields (e.g. subtract:timestamp) are not permitted in Goby v2 --> v1 backwards compatibility. Please redesign the XML message to remove such algorithms. Offending algorithm: " + algorithm + " used in variable: " + var->name()));
        }
        
        
        new_alg->set_name(algorithm);
        new_alg->set_primary_field(var->sequence_number());
    }
            
}


std::ostream& goby::transitional::operator<< (std::ostream& out, const std::set<std::string>& s)
{
    out << "std::set<std::string>:" << std::endl;
    for (std::set<std::string>::const_iterator it = s.begin(), n = s.end(); it != n; ++it)
        out << (*it) << std::endl;
    return out;
}

std::ostream& goby::transitional::operator<< (std::ostream& out, const std::set<unsigned>& s)
{
    out << "std::set<unsigned>:" << std::endl;
    for (std::set<unsigned>::const_iterator it = s.begin(), n = s.end(); it != n; ++it)
        out << (*it) << std::endl;
    return out;
}


/////////////////////
// private methods
/////////////////////


void goby::transitional::DCCLTransitionalCodec::check_duplicates()
{
    std::map<unsigned, std::vector<DCCLMessage>::iterator> all_ids;
    for(std::vector<DCCLMessage>::iterator it = messages_.begin(), n = messages_.end(); it != n; ++it)
    {
        unsigned id = it->id();
            
        std::map<unsigned, std::vector<DCCLMessage>::iterator>::const_iterator id_it = all_ids.find(id);
        if(id_it != all_ids.end())
            throw goby::acomms::DCCLException(std::string("DCCL: duplicate variable id " + as<std::string>(id) + " specified for " + it->name() + " and " + id_it->second->name()));
            
        all_ids.insert(std::pair<unsigned, std::vector<DCCLMessage>::iterator>(id, it));
    }
}

std::vector<goby::transitional::DCCLMessage>::const_iterator goby::transitional::DCCLTransitionalCodec::to_iterator(const std::string& message_name) const
{
    if(name2messages_.count(message_name))
        return messages_.begin() + name2messages_.find(message_name)->second;
    else
        throw goby::acomms::DCCLException(std::string("DCCL: attempted an operation on message [" + message_name + "] which is not loaded"));
}
std::vector<goby::transitional::DCCLMessage>::iterator goby::transitional::DCCLTransitionalCodec::to_iterator(const std::string& message_name)
{
    if(name2messages_.count(message_name))
        return messages_.begin() + name2messages_.find(message_name)->second;
    else
        throw goby::acomms::DCCLException(std::string("DCCL: attempted an operation on message [" + message_name + "] which is not loaded"));
}
std::vector<goby::transitional::DCCLMessage>::const_iterator goby::transitional::DCCLTransitionalCodec::to_iterator(const unsigned& id) const
{
    if(id2messages_.count(id))
        return messages_.begin() + id2messages_.find(id)->second;
    else
        throw goby::acomms::DCCLException(std::string("DCCL: attempted an operation on message [" + as<std::string>(id) + "] which is not loaded"));
}

std::vector<goby::transitional::DCCLMessage>::iterator goby::transitional::DCCLTransitionalCodec::to_iterator(const unsigned& id)
{
    if(id2messages_.count(id))
        return messages_.begin() + id2messages_.find(id)->second;
    else
        throw goby::acomms::DCCLException(std::string("DCCL: attempted an operation on message [" + as<std::string>(id) + "] which is not loaded"));
}



