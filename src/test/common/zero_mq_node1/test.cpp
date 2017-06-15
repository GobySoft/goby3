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

// tests basic publish/subscribe over EPGM functionality of ZeroMQService class

#include "goby/common/zeromq_service.h"

#include <boost/thread.hpp>

void node_inbox(goby::common::MarshallingScheme marshalling_scheme,
                const std::string& identifier,
                const std::string& data,
                int socket_id);

const std::string identifier_ = "HI/";
int inbox_count_ = 0;
const char data_ [] = {'h', 'i', '\0'};

enum {
    SOCKET_SUBSCRIBE = 240, SOCKET_PUBLISH = 211
};
    

int main(int argc, char* argv[])
{
    goby::glog.add_stream(goby::common::logger::DEBUG3, &std::cerr);
    goby::glog.set_name(argv[0]);
    
    goby::common::ZeroMQService node;

    goby::common::protobuf::ZeroMQServiceConfig pubsub_cfg;

    {
            
        goby::common::protobuf::ZeroMQServiceConfig::Socket* subscriber_socket = pubsub_cfg.add_socket();
        subscriber_socket->set_socket_type(goby::common::protobuf::ZeroMQServiceConfig::Socket::SUBSCRIBE);
        subscriber_socket->set_socket_id(SOCKET_SUBSCRIBE);
        subscriber_socket->set_ethernet_address("127.0.0.1");
        std::cout << subscriber_socket->DebugString() << std::endl;
    }

    {
            
        goby::common::protobuf::ZeroMQServiceConfig::Socket* publisher_socket = pubsub_cfg.add_socket();
        publisher_socket->set_socket_type(goby::common::protobuf::ZeroMQServiceConfig::Socket::PUBLISH);
        publisher_socket->set_ethernet_address("127.0.0.1");
        publisher_socket->set_socket_id(SOCKET_PUBLISH);
        std::cout << publisher_socket->DebugString() << std::endl;
    }
    

    
    node.set_cfg(pubsub_cfg);
    node.connect_inbox_slot(&node_inbox);
    node.subscribe_all(SOCKET_SUBSCRIBE);

    usleep(1e3);

    int test_count = 3;
    for(int i = 0; i < test_count; ++i)
    {
        std::cout << "publishing " << data_ << std::endl;
        node.send(goby::common::MARSHALLING_CSTR, identifier_, data_, SOCKET_PUBLISH);
        node.poll(1e6);
    }

    assert(inbox_count_ == test_count);
    
    std::cout << "all tests passed" << std::endl;
}


void node_inbox(goby::common::MarshallingScheme marshalling_scheme,
                const std::string& identifier,
                const std::string& data,
                int socket_id)
{
    assert(identifier == identifier_);
    assert(marshalling_scheme == goby::common::MARSHALLING_CSTR);
    assert(!strcmp(data.c_str(), data_));
    assert(socket_id == SOCKET_SUBSCRIBE);
    
    std::cout << "Received: " << data << std::endl;
    ++inbox_count_;
}
