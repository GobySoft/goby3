#include "transport-interprocess.h"

unsigned goby::ZMQRouter::last_port(zmq::socket_t& socket)
{
    size_t last_endpoint_size = 100;
    char last_endpoint[last_endpoint_size];
    int rc = zmq_getsockopt (socket, ZMQ_LAST_ENDPOINT, &last_endpoint, &last_endpoint_size);
                
    if(rc != 0)
        throw(std::runtime_error("Could not retrieve ZMQ_LAST_ENDPOINT"));

    std::string last_ep(last_endpoint);
    unsigned port = std::stoi(last_ep.substr(last_ep.find_last_of(":")+1));
    return port;
}


void goby::ZMQRouter::run()
{
    zmq::socket_t frontend(context_, ZMQ_XPUB);
    zmq::socket_t backend(context_, ZMQ_XSUB);

    int send_hwm = cfg_.send_queue_size();
    int receive_hwm = cfg_.receive_queue_size();
    frontend.setsockopt(ZMQ_SNDHWM, &send_hwm, sizeof(send_hwm));
    backend.setsockopt(ZMQ_SNDHWM, &send_hwm, sizeof(send_hwm));
    frontend.setsockopt(ZMQ_RCVHWM, &receive_hwm, sizeof(receive_hwm));
    backend.setsockopt(ZMQ_RCVHWM, &receive_hwm, sizeof(receive_hwm));

    switch(cfg_.transport())
    {
        case goby::protobuf::InterProcessPortalConfig::IPC:
        {
            std::string xpub_sock_name = "ipc://" + (cfg_.has_socket_name() ? cfg_.socket_name() : "/tmp/goby_" + cfg_.platform()) + ".xpub";
            std::string xsub_sock_name = "ipc://" + (cfg_.has_socket_name() ? cfg_.socket_name() : "/tmp/goby_" + cfg_.platform()) + ".xsub";
            frontend.bind(xpub_sock_name.c_str());
            backend.bind(xsub_sock_name.c_str());
            break;
        }
        case goby::protobuf::InterProcessPortalConfig::TCP:
        {
            frontend.bind("tcp://*:0");
            backend.bind("tcp://*:0");
            pub_port = last_port(frontend);    
            sub_port = last_port(backend);
            break;
        }
    }
    try
    {
        zmq::proxy(frontend, backend, nullptr);
    }
    catch(const zmq::error_t& e)
    {
        // context terminated
        if(e.num() == ETERM)
            return;
        else
            throw(e);
    }
}

void goby::ZMQManager::run()
{
    zmq::socket_t socket (context_, ZMQ_REP);

    switch(cfg_.transport())
    {
        case goby::protobuf::InterProcessPortalConfig::IPC:
        {
            std::string sock_name = "ipc://" + ((cfg_.has_socket_name() ? cfg_.socket_name() : "/tmp/goby_" + cfg_.platform()) + ".manager");
            socket.bind(sock_name.c_str());
            break;
        }
        case goby::protobuf::InterProcessPortalConfig::TCP:
        {
            std::string sock_name = "tcp://*:" + std::to_string(cfg_.tcp_port());
            socket.bind(sock_name.c_str());
            break;
        }
    }
    
    try
    {
        while (true)
        {
            zmq::message_t request;
            socket.recv (&request);

            goby::protobuf::ZMQManagerRequest pb_request;
            const int packet_header_size = 1; // '\0'
            pb_request.ParseFromArray((char*)request.data()+packet_header_size, request.size()-packet_header_size);

            while(cfg_.transport() == goby::protobuf::InterProcessPortalConfig::TCP && (router_.pub_port == 0 || router_.sub_port == 0))
                usleep(1e4);

            goby::protobuf::ZMQManagerResponse pb_response;
            pb_response.set_request(pb_request.request());

            if(pb_request.request() == goby::protobuf::PROVIDE_PUB_SUB_SOCKETS)
            {
                goby::common::protobuf::ZeroMQServiceConfig::Socket* subscribe_socket = pb_response.mutable_subscribe_socket();
                goby::common::protobuf::ZeroMQServiceConfig::Socket* publish_socket = pb_response.mutable_publish_socket();
                subscribe_socket->set_socket_type(goby::common::protobuf::ZeroMQServiceConfig::Socket::SUBSCRIBE);
                publish_socket->set_socket_type(goby::common::protobuf::ZeroMQServiceConfig::Socket::PUBLISH);
                subscribe_socket->set_connect_or_bind(goby::common::protobuf::ZeroMQServiceConfig::Socket::CONNECT);
                publish_socket->set_connect_or_bind(goby::common::protobuf::ZeroMQServiceConfig::Socket::CONNECT);

                subscribe_socket->set_send_queue_size(cfg_.send_queue_size());
                subscribe_socket->set_receive_queue_size(cfg_.receive_queue_size());
                publish_socket->set_send_queue_size(cfg_.send_queue_size());
                publish_socket->set_receive_queue_size(cfg_.receive_queue_size());
                
                switch(cfg_.transport())
                {
                    case goby::protobuf::InterProcessPortalConfig::IPC:
                        subscribe_socket->set_transport(goby::common::protobuf::ZeroMQServiceConfig::Socket::IPC);
                        publish_socket->set_transport(goby::common::protobuf::ZeroMQServiceConfig::Socket::IPC);
                        subscribe_socket->set_socket_name((cfg_.has_socket_name() ? cfg_.socket_name() : "/tmp/goby_" + cfg_.platform()) + ".xpub");
                        publish_socket->set_socket_name((cfg_.has_socket_name() ? cfg_.socket_name() : "/tmp/goby_" + cfg_.platform()) + ".xsub");
                        break;
                    case goby::protobuf::InterProcessPortalConfig::TCP:
                        subscribe_socket->set_transport(goby::common::protobuf::ZeroMQServiceConfig::Socket::TCP);
                        publish_socket->set_transport(goby::common::protobuf::ZeroMQServiceConfig::Socket::TCP);
                        subscribe_socket->set_ethernet_port(router_.pub_port); // our publish is their subscribe
                        publish_socket->set_ethernet_port(router_.sub_port);
                        break;
                }
            }
            
            zmq::message_t reply(pb_response.ByteSize() + packet_header_size);
            pb_response.SerializeToArray((char*)reply.data() + packet_header_size, reply.size() - packet_header_size);

            std::array<char, packet_header_size> header { '\0' };
            
            memcpy(reply.data(), &header[0], packet_header_size);
            socket.send(reply);
            
            std::cout << pb_response.DebugString() << std::endl;
        }
    }
    catch(const zmq::error_t& e)
    {
        // context terminated
        if(e.num() == ETERM)
            return;
        else
            throw(e);
    }
}
