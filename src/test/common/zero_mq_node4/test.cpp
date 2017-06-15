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

// tests blackout functionality of ZeroMQService

#include "goby/common/zeromq_service.h"

#include <boost/thread.hpp>

void node_inbox(goby::common::MarshallingScheme marshalling_scheme,
                const std::string& identifier,
                const std::string& data,
                int socket_id);


void run_basic_test(int test_count, int expected_blackouts, int ms_wait);


const std::string identifier_ = "HI/";
int inbox_count_ = 0;
const char data_ [] = {'h', 'i', '\0'};

goby::common::ZeroMQService node1_;
// must share context for ipc
goby::common::ZeroMQService node2_(node1_.zmq_context());

enum {
    SOCKET_SUBSCRIBE = 240, SOCKET_PUBLISH = 211
};
    

int main(int argc, char* argv[])
{
    goby::glog.add_stream(goby::common::logger::DEBUG3, &std::cerr);
    goby::glog.set_name(argv[0]);
    

    goby::common::protobuf::ZeroMQServiceConfig publisher_cfg, subscriber_cfg;
    {
            
        goby::common::protobuf::ZeroMQServiceConfig::Socket* subscriber_socket = subscriber_cfg.add_socket();
        subscriber_socket->set_socket_type(goby::common::protobuf::ZeroMQServiceConfig::Socket::SUBSCRIBE);
        subscriber_socket->set_transport(goby::common::protobuf::ZeroMQServiceConfig::Socket::IPC);
        subscriber_socket->set_connect_or_bind(goby::common::protobuf::ZeroMQServiceConfig::Socket::CONNECT);

        subscriber_socket->set_socket_id(SOCKET_SUBSCRIBE);
        subscriber_socket->set_socket_name("test3_ipc_socket");
        std::cout << subscriber_socket->DebugString() << std::endl;
    }

    {
            
        goby::common::protobuf::ZeroMQServiceConfig::Socket* publisher_socket = publisher_cfg.add_socket();
        publisher_socket->set_socket_type(goby::common::protobuf::ZeroMQServiceConfig::Socket::PUBLISH);
        publisher_socket->set_transport(goby::common::protobuf::ZeroMQServiceConfig::Socket::IPC);
        publisher_socket->set_connect_or_bind(goby::common::protobuf::ZeroMQServiceConfig::Socket::BIND);
        publisher_socket->set_socket_name("test3_ipc_socket");
        publisher_socket->set_socket_id(SOCKET_PUBLISH);
        std::cout << publisher_socket->DebugString() << std::endl;
    }
    

    
    node1_.set_cfg(publisher_cfg);

    node2_.set_cfg(subscriber_cfg);
    node2_.connect_inbox_slot(&node_inbox);
    node2_.subscribe_all(SOCKET_SUBSCRIBE);

    usleep(1e3);


    // test local blackout
    node2_.socket_from_id(SOCKET_SUBSCRIBE).set_blackout(goby::common::MARSHALLING_CSTR, identifier_, boost::posix_time::milliseconds(6));
    run_basic_test(3, 1, 5);
    run_basic_test(8, 4, 4); 

    // remove local blackout
    node2_.socket_from_id(SOCKET_SUBSCRIBE).clear_blackout(goby::common::MARSHALLING_CSTR, identifier_);
    run_basic_test(3, 0, 5);    
    run_basic_test(8, 0, 4); 
    

    // add global blackout
    node2_.socket_from_id(SOCKET_SUBSCRIBE).set_global_blackout(boost::posix_time::milliseconds(6));
    run_basic_test(3, 1, 5);    
    run_basic_test(8, 4, 4); 

    // remove global blackout
    node2_.socket_from_id(SOCKET_SUBSCRIBE).clear_global_blackout();
    run_basic_test(3, 0, 5);    
    run_basic_test(8, 0, 4); 

    // test both at once - local blackout will take precedence
    node2_.socket_from_id(SOCKET_SUBSCRIBE).set_global_blackout(boost::posix_time::milliseconds(6));
    node2_.socket_from_id(SOCKET_SUBSCRIBE).set_blackout(goby::common::MARSHALLING_CSTR, identifier_, boost::posix_time::milliseconds(0));
    run_basic_test(3, 0, 5);
    run_basic_test(8, 0, 4); 

    
    std::cout << "all tests passed" << std::endl;
}

void run_basic_test(int test_count, int expected_blackouts, int ms_wait)
{
    inbox_count_ = 0;
    for(int i = 0; i < test_count; ++i)
    {
        std::cout << "publishing " << data_ << std::endl;
        node1_.send(goby::common::MARSHALLING_CSTR, identifier_, std::string(data_), SOCKET_PUBLISH);
        node2_.poll(1e6);
        // wait ms_wait milliseconds
        usleep(ms_wait*1e3);
    }

    assert(inbox_count_ == test_count-expected_blackouts);
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
