// Copyright 2009-2018 Toby Schneider (http://gobysoft.org/index.wt/people/toby)
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

#include "goby/util/dynamic_protobuf_manager.h"
#include <cassert>
#include <dlfcn.h>
#include <google/protobuf/descriptor.pb.h>
#include <google/protobuf/text_format.h>
#include <iostream>

#include "test_a.pb.h"

using namespace goby::util;

int main()
{
    void* dl_handle = dlopen("libtest_dyn_protobuf" SHARED_LIBRARY_SUFFIX, RTLD_LAZY);

    if (!dl_handle)
    {
        std::cerr << "Failed to open libtest_dyn_protobuf" SHARED_LIBRARY_SUFFIX << std::endl;
        exit(1);
    }

    boost::shared_ptr<google::protobuf::SimpleDescriptorDatabase> simple_database(
        new google::protobuf::SimpleDescriptorDatabase);
    goby::util::DynamicProtobufManager::add_database(simple_database);

    {
        // testing compiled in
        boost::shared_ptr<google::protobuf::Message> adyn_msg =
            goby::util::DynamicProtobufManager::new_protobuf_message("A");

        std::cout << adyn_msg->GetDescriptor()->DebugString() << std::endl;

        // testing dlopen'd
        boost::shared_ptr<google::protobuf::Message> bdyn_msg =
            goby::util::DynamicProtobufManager::new_protobuf_message("B");

        std::cout << bdyn_msg->GetDescriptor()->DebugString() << std::endl;

        // test non-existent
        try
        {
            boost::shared_ptr<google::protobuf::Message> cdyn_msg =
                goby::util::DynamicProtobufManager::new_protobuf_message("C");
            // should throw
            assert(false);
        }
        catch (std::exception& e)
        {
            // expected
        }

        // test dynamically loaded
        google::protobuf::FileDescriptorProto d_proto;
        std::string d_proto_str = "name: \"goby/test/util/dynamic_protobuf/test_d.proto\" "
                                  "message_type {   name: \"D\"   field {     name: \"d1\"     "
                                  "number: 1     label: LABEL_REQUIRED     type: TYPE_DOUBLE  } } ";

        google::protobuf::TextFormat::ParseFromString(d_proto_str, &d_proto);
        goby::util::DynamicProtobufManager::add_protobuf_file(d_proto);

        boost::shared_ptr<google::protobuf::Message> ddyn_msg =
            goby::util::DynamicProtobufManager::new_protobuf_message("D");

        std::cout << ddyn_msg->GetDescriptor()->DebugString() << std::endl;

        // test dynamically via separate database
        google::protobuf::FileDescriptorProto e_proto;
        std::string e_proto_str = "name: \"goby/test/util/dynamic_protobuf/test_e.proto\" "
                                  "message_type {   name: \"E\"   field {     name: \"e1\"     "
                                  "number: 1     label: LABEL_REQUIRED     type: TYPE_DOUBLE  } } ";

        google::protobuf::TextFormat::ParseFromString(e_proto_str, &e_proto);

        simple_database->Add(e_proto);

        boost::shared_ptr<google::protobuf::Message> edyn_msg =
            goby::util::DynamicProtobufManager::new_protobuf_message("E");
        std::cout << edyn_msg->GetDescriptor()->DebugString() << std::endl;

        std::cout << "all tests passed" << std::endl;
    }

    goby::util::DynamicProtobufManager::protobuf_shutdown();

    dlclose(dl_handle);
    return 0;
}
