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

// based initially on work in C++ Cookbook by D. Ryan Stephens, Christopher Diggins, Jonathan Turkanis, and Jeff Cogswell. Copyright 2006 O'Reilly Media, INc., 0-596-00761-2.

#ifndef XML_PARSER20091211H
#define XML_PARSER20091211H


#include <exception>
#include <iostream>     // cout
#include <memory>       
#include <vector>
#include <fstream>

#include <xercesc/sax2/SAX2XMLReader.hpp>
#include <xercesc/sax2/XMLReaderFactory.hpp>
#include <xercesc/util/PlatformUtils.hpp>
#include <xercesc/sax2/DefaultHandler.hpp>

#include "xerces_strings.h" 
#include "message_schema.xsd.h"

    
// RAII utility that initializes the parser and frees resources
// when it goes out of scope
class XercesInitializer {
  public:
    XercesInitializer( ) { xercesc::XMLPlatformUtils::Initialize( ); }
    ~XercesInitializer( ) { xercesc::XMLPlatformUtils::Terminate( ); }
  private:
    // Prohibit copying and assignment
    XercesInitializer(const XercesInitializer&);
    XercesInitializer& operator=(const XercesInitializer&);
};

class XMLParser {
  public:
  XMLParser(xercesc::DefaultHandler& content, xercesc::DefaultHandler& error)
      : content_(content),
        error_(error) 
        { }
        
    bool parse(const std::string& file)
    {
        // Initialize Xerces and obtain parser
        XercesInitializer init; 
        std::unique_ptr<xercesc::SAX2XMLReader> parser(xercesc::XMLReaderFactory::createXMLReader());

        // write XML schema to file
        const std::string schema = "/tmp/dccl_message_schema.xsd";
        std::ofstream fout;
        fout.open(schema.c_str());
        for(unsigned i = 0; i < message_schema_xsd_len; ++i)
            fout << message_schema_xsd[i];
        fout.close();
        
        const XMLCh* xs_schema = fromNative(schema.c_str());
        const XMLCh* const schema_location = xs_schema;
        parser->setFeature(xercesc::XMLUni::fgSAX2CoreValidation, true);
        parser->setProperty(xercesc::XMLUni::fgXercesSchemaExternalNoNameSpaceSchemaLocation, (void*)schema_location);
        
        parser->setContentHandler(&content_);
        parser->setErrorHandler(&error_);
        
        // Parse the XML document
        parser->parse(file.c_str());
        
        return true;
    }       
        
  private:
    xercesc::DefaultHandler& content_;
    xercesc::DefaultHandler& error_;
};




#endif
