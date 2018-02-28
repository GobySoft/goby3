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

// tests basic REP/REQ functionality of ZeroMQService class

#include "goby/common/zeromq_service.h"

void inbox(int marshalling_scheme,
           const std::string& identifier,
           const std::string& data,
           int socket_id);


const std::string reply_identifier_ = "RESPONSE/";
const std::string request_identifier_ = "REQUEST/";
int inbox_count_ = 0;
const char request_data_ [] = {'h', 'i', '\0'};    
const char reply_data_ [] = {'w', 'e', 'l', 'l',' ', 'h', 'e', 'l', 'l','o', '\0'};    

// pick some misc values
enum { SOCKET_REQUESTOR = 104952, SOCKET_REPLIER = 2304 
};

    

int main(int argc, char* argv[])
{
    goby::glog.add_stream(goby::common::logger::DEBUG3, &std::cerr);
    goby::glog.set_name(argv[0]);

    goby::common::ZeroMQService node;

    {
        using goby::common::protobuf::ZeroMQServiceConfig;
        
        ZeroMQServiceConfig requestor_cfg;
        ZeroMQServiceConfig::Socket* requestor_socket = requestor_cfg.add_socket();
        requestor_socket->set_socket_type(ZeroMQServiceConfig::Socket::REQUEST);
        requestor_socket->set_transport(ZeroMQServiceConfig::Socket::TCP);
        requestor_socket->set_socket_id(SOCKET_REQUESTOR);
        requestor_socket->set_ethernet_address("127.0.0.1");
        requestor_socket->set_ethernet_port(54321);
        requestor_socket->set_connect_or_bind(ZeroMQServiceConfig::Socket::CONNECT);
        std::cout << requestor_socket->DebugString() << std::endl;

        ZeroMQServiceConfig replier_cfg;
        ZeroMQServiceConfig::Socket* replier_socket = replier_cfg.add_socket();
        replier_socket->set_socket_type(ZeroMQServiceConfig::Socket::REPLY);
        replier_socket->set_transport(ZeroMQServiceConfig::Socket::TCP);
        replier_socket->set_ethernet_port(54321);
        replier_socket->set_connect_or_bind(ZeroMQServiceConfig::Socket::BIND);
        replier_socket->set_socket_id(SOCKET_REPLIER);

        std::cout << replier_socket->DebugString() << std::endl;
    
        node.merge_cfg(requestor_cfg);
        node.merge_cfg(replier_cfg);
    }
    
    node.connect_inbox_slot(&inbox);

    usleep(1e4);

    int test_count = 3;
    for(int i = 0; i < test_count; ++i)
    {
        std::cout << "requesting " << request_data_ << std::endl;
        node.send(goby::common::MARSHALLING_CSTR, request_identifier_, std::string(request_data_), SOCKET_REQUESTOR);
        node.poll(1e6);
        std::cout << "replying " << reply_data_ << std::endl;
        node.send(goby::common::MARSHALLING_CSTR, reply_identifier_, std::string(reply_data_), SOCKET_REPLIER);
        node.poll(1e6);
    }

    assert(inbox_count_ == 2*test_count);
    
    std::cout << "all tests passed" << std::endl;
}


void inbox(int marshalling_scheme,
           const std::string& identifier,
           const std::string& data,
           int socket_id)
{

    if(socket_id == SOCKET_REPLIER)
    {        
        assert(identifier == request_identifier_);
        assert(marshalling_scheme == goby::common::MARSHALLING_CSTR);
        assert(!strcmp(data.c_str(), request_data_));
    
        std::cout << "Replier Received: " << data << std::endl;
        ++inbox_count_;
    }
    else if(socket_id == SOCKET_REQUESTOR)
    {
        assert(identifier == reply_identifier_);
        assert(marshalling_scheme == goby::common::MARSHALLING_CSTR);
        assert(!strcmp(data.c_str(), reply_data_));
        
        std::cout << "Requestor Received: " << data << std::endl;
        ++inbox_count_;
    }
    else
    {
        assert(false);
    }
    
}
