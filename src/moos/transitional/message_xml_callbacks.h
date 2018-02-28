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

// xml code based initially on work in C++ Cookbook by D. Ryan Stephens, Christopher Diggins, Jonathan Turkanis, and Jeff Cogswell. Copyright 2006 O'Reilly Media, INc., 0-596-00761-2

#ifndef MESSAGE_XML_CALLBACKS20091211H
#define MESSAGE_XML_CALLBACKS20091211H

#include <vector>
#include <sstream>

#include <xercesc/sax2/Attributes.hpp>
#include <xercesc/sax2/DefaultHandler.hpp> 
#include <boost/algorithm/string.hpp> // for string functions

#include "goby/moos/transitional/xml/xerces_strings.h"

#include "message.h"
#include "goby/acomms/dccl.h"
#include "goby/moos/transitional/xml/tags.h"

namespace goby
{
    namespace transitional
    {
        class DCCLMessage;
        
// Implements callbacks that receive character data and
// notifications about the beginnings and ends of elements 
        class DCCLMessageContentHandler : public xercesc::DefaultHandler {
          public:
          DCCLMessageContentHandler(std::vector<DCCLMessage> & messages) : messages(messages)
            { initialize_tags(tags_map_); }

            void startElement( 
                const XMLCh *const uri,        // namespace URI
                const XMLCh *const localname,  // tagname w/ out NS prefix
                const XMLCh *const qname,      // tagname + NS pefix
                const xercesc::Attributes &attrs );      // elements's attributes

            void endElement(          
                const XMLCh *const uri,        // namespace URI
                const XMLCh *const localname,  // tagname w/ out NS prefix
                const XMLCh *const qname );     // tagname + NS pefix

#if XERCES_VERSION_MAJOR < 3
            void characters(const XMLCh* const chars, const unsigned int length)
	      { current_text.append(toNative(chars), 0, length); }
#else
            void characters(const XMLCh* const chars, const XMLSize_t length )
	      { current_text.append(toNative(chars), 0, length); }
#endif

          private:
            bool in_message_var()
            { return xml::in_message_var(parents_); }
            bool in_header_var()
            { return xml::in_header_var(parents_); }
            bool in_publish()
            { return xml::in_publish(parents_); }
        
          private:
            std::vector<DCCLMessage>& messages;
            std::string current_text;

            std::set<xml::Tag> parents_;
            std::map<std::string, xml::Tag> tags_map_;

            transitional::DCCLHeaderPart curr_head_piece_;
        
        };

// Receives Error notifications.
        class DCCLMessageErrorHandler : public xercesc::DefaultHandler 
        {

          public:
            void warning(const xercesc::SAXParseException& e)
            {
                std::cout << "warning:" << toNative(e.getMessage());
            }
            void error(const xercesc::SAXParseException& e)
            {
                XMLSSize_t line = e.getLineNumber();
                std::stringstream ss;
                ss << "message xml parsing error on line " << line << ": " << std::endl << toNative(e.getMessage());
            
                throw goby::acomms::DCCLException(ss.str());
            }
            void fatalError(const xercesc::SAXParseException& e) 
            {
                error(e);
            }
        };
    }
}
#endif
