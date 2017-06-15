// Copyright 2009-2017 Toby Schneider (http://gobysoft.org/index.wt/people/toby)
//                     GobySoft, LLC (2013-)
//                     Massachusetts Institute of Technology (2007-2014)
//
//
// This file is part of the Goby Underwater Autonomy Project Binaries
// ("The Goby Binaries").
//
// The Goby Binaries are free software: you can redistribute them and/or modify
// them under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 2 of the License, or
// (at your option) any later version.
//
// The Goby Binaries are distributed in the hope that they will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with Goby.  If not, see <http://www.gnu.org/licenses/>.

#include "goby/moos/transitional/dccl_transitional.h"
#include "goby/util/dynamic_protobuf_manager.h"

int main(int argc, char* argv[])
{
    std::string xml_file;
    std::string proto_folder = "";
    
    switch(argc)
    {
        case 3:
            xml_file = argv[1];
            proto_folder = argv[2];
        
        case 2:
            xml_file = argv[1];
            break;

        default:
            std::cerr << "usage: dccl_xml_to_dccl_proto message_xml_file.xml [directory for generated .proto (default = pwd (.)]" << std::endl;
            exit(EXIT_FAILURE);
    }

    goby::glog.add_stream(goby::common::protobuf::GLogConfig::VERBOSE, &std::cerr);
    
    std::cerr << "creating DCCLTransitionalCodec using xml file: [" << xml_file << "]" << std::endl;
    
    goby::transitional::DCCLTransitionalCodec dccl;

    goby::util::DynamicProtobufManager::enable_compilation();
    
    pAcommsHandlerConfig cfg;    
    cfg.mutable_transitional_cfg()->add_message_file()->set_path(xml_file);
    cfg.mutable_transitional_cfg()->set_generated_proto_dir(proto_folder);
    dccl.convert_to_v2_representation(&cfg);
    
    std::cout << "received: " << cfg.DebugString();
    
    std::cerr << "wrote proto files" << std::endl;
}
