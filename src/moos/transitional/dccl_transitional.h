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

#ifndef DCCLTRANSITIONAL20091211H
#define DCCLTRANSITIONAL20091211H

#include <string>
#include <set>
#include <map>
#include <ostream>
#include <stdexcept>
#include <vector>

#include "goby/moos/transitional/xml/xml_parser.h"
#include "goby/common/time.h"
#include "goby/common/logger.h"

#include "message.h"
#include "message_val.h"
#include "goby/moos/protobuf/transitional.pb.h"
#include "goby/moos/protobuf/pAcommsHandler_config.pb.h"
#include "goby/acomms/protobuf/queue.pb.h"
#include "goby/acomms/protobuf/modem_message.pb.h"
#include "goby/acomms/acomms_helpers.h"
#include "goby/acomms/dccl.h"

#include <google/protobuf/dynamic_message.h>
#include <google/protobuf/compiler/importer.h>
#include <google/protobuf/descriptor.h>

/// The global namespace for the Goby project.
namespace goby
{
    namespace util
    { class FlexOstream; }


    /// Objects pertaining to transitioning from DCCLv1 to DCCLv2
    namespace transitional
    {
        /// use this for displaying a human readable version
        template<typename Value>
            std::ostream& operator<< (std::ostream& out, const std::map<std::string, Value>& m)
        {
            typedef std::pair<std::string, Value> P;
            BOOST_FOREACH(const P& p, m)
            {
                out << "\t" << "key: " << p.first << std::endl
                    << "\t" << "value: " << p.second << std::endl;
            }
            return out;
        }

        template<typename Value>
            std::ostream& operator<< (std::ostream& out, const std::multimap<std::string, Value>& m)
        {
            typedef std::pair<std::string, Value> P;
            BOOST_FOREACH(const P& p, m)
            {
                out << "\t" << "key: " << p.first << std::endl
                    << "\t" << "value: " << p.second << std::endl;
            }
            return out;
        }

    
        /// use this for displaying a human readable version of this STL object
        std::ostream& operator<< (std::ostream& out, const std::set<unsigned>& s);
        /// use this for displaying a human readable version of this STL object
        std::ostream& operator<< (std::ostream& out, const std::set<std::string>& s);

        /// \class DCCLTransitionalCodec dccl.h goby/acomms/dccl.h
        /// \brief provides an API to the Transitional Dynamic CCL Codec (looks like DCCLv1, but calls DCCLv2). Warning: this class is for legacy support only, new applications should use DCCLCodec directly.
        /// \ingroup acomms_api
        /// \sa transitional.proto and acomms_modem_message.proto for definition of Google Protocol Buffers messages (namespace goby::transitional::protobuf).

        class DCCLTransitionalCodec 
        {
          public:
            /// \name Constructors/Destructor
            //@{         
            /// \brief Instantiate optionally with a ostream logger (for human readable output)
            /// \param log std::ostream object or FlexOstream to capture all humanly readable runtime and debug information (optional).
            DCCLTransitionalCodec();

            /// destructor
            ~DCCLTransitionalCodec() {}
            //@}

            
            
            /// \name Initialization Methods.
            ///
            /// These methods are intended to be called before doing any work with the class. However,
            /// they may be called at any time as desired.
            //@{

            void convert_to_v2_representation(pAcommsHandlerConfig* cfg);
            
            //@}        

            template<typename Key>
                const google::protobuf::Descriptor* descriptor(const Key& k)
            {  return to_iterator(k)->descriptor(); }                

            /// how many message are loaded?
            /// \return number of messages loaded
            unsigned message_count() { return messages_.size(); }

            /// \return repeat value (number of copies of the message per encode)
            template<typename Key>
                unsigned get_repeat(const Key& k)
            { return to_iterator(k)->repeat(); }
        
            /// \return set of all message ids loaded
            std::set<unsigned> all_message_ids();
            /// \return set of all message names loaded
            std::set<std::string> all_message_names();
            /// \return map of names to DCCL types needed to encode a given message
            template<typename Key>
                std::map<std::string, std::string> message_var_names(const Key& k) const
            { return to_iterator(k)->message_var_names(); }
        
            /// \param id message id
            /// \return name of message
            std::string id2name(unsigned id) {return to_iterator(id)->name();}
            /// \param name message name
            /// \return id of message
            unsigned name2id(const std::string& name) {return to_iterator(name)->id();}
            
            //@}
        
            // returns a reference to all DCCLMessage objects.

            // this is only used if one needs more control than DCCLTransitionalCodec
            // provides
            std::vector<DCCLMessage>& messages() {return messages_;}

            // grab a reference to the manipulator manager used by the loaded XML messages
//            const ManipulatorManager& manip_manager() const { return manip_manager_; }
        
          private:
            void convert_xml_message_file(const goby::transitional::protobuf::MessageFile& message_file,
                                          std::string* proto_file,
                                          google::protobuf::RepeatedPtrField<goby::moos::protobuf::TranslatorEntry>* translator_entries,
                                          goby::acomms::protobuf::QueueManagerConfig* queue_cfg);
            
            

            void fill_create(boost::shared_ptr<DCCLMessageVar> var,
                             std::map<std::string, goby::moos::protobuf::TranslatorEntry::CreateParser*>* parser_map,
                             goby::moos::protobuf::TranslatorEntry* entry);

            
            std::vector<DCCLMessage>::const_iterator to_iterator(const std::string& message_name) const;
            std::vector<DCCLMessage>::iterator to_iterator(const std::string& message_name);
            std::vector<DCCLMessage>::const_iterator to_iterator(const unsigned& id) const;
            std::vector<DCCLMessage>::iterator to_iterator(const unsigned& id);
        
            void check_duplicates();            
            
          private:
            std::ostream* log_;
            goby::acomms::DCCLCodec* dccl_;

            std::vector<DCCLMessage> messages_;

            std::map<std::string, size_t>  name2messages_;
            std::map<unsigned, size_t>     id2messages_;

            protobuf::DCCLTransitionalConfig cfg_;

            boost::posix_time::ptime start_time_;
            
//            ManipulatorManager manip_manager_;
        };
        
    }
}


#endif
