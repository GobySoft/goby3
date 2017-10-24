// Copyright 2009-2016 Toby Schneider (http://gobysoft.org/index.wt/people/toby)
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

#include "goby/util/binary.h" // for hex_encode
#include "goby/common/logger.h" // for glog & manipulators die, warn, group(), etc.
#include "goby/util/as.h" // for goby::util::as

#include "zeromq_service.h"
#include "goby/common/exception.h"

using goby::util::as;
using goby::glog;
using goby::util::hex_encode;
using namespace goby::common::logger_lock;
using namespace goby::common::logger;

#if ZMQ_VERSION_MAJOR == 2
#   define zmq_msg_send(msg,sock,opt) zmq_send (sock, msg, opt)
#   define zmq_msg_recv(msg,sock,opt) zmq_recv (sock, msg, opt)
#   define ZMQ_POLL_DIVISOR    1        //  zmq_poll is usec
#   define more_t int64_t
#else
#   define more_t int
#   define ZMQ_POLL_DIVISOR    1000     //  zmq_poll is msec
#endif

goby::middleware::ZeroMQService::ZeroMQService(std::shared_ptr<zmq::context_t> context)
    : context_(context)
{
    glog.add_group(glog_out_group(), common::Colors::lt_magenta);
    glog.add_group(glog_in_group(), common::Colors::lt_blue);
}

goby::middleware::ZeroMQService::ZeroMQService()
    : ZeroMQService(std::shared_ptr<zmq::context_t>(new zmq::context_t(2)))
{ }


// writes sockets_, poll_items_, poll_item_to_socket_
void goby::middleware::ZeroMQService::merge_cfg(common::protobuf::ZeroMQServiceConfig* cfg)
{
    glog.is(DEBUG1) && glog << "Entering ZeroMQService::merge_cfg" << std::endl;

    std::unique_lock<decltype(mutex_)> lock(mutex_);
    while(reader_count_ > 0) 
        writer_cv_.wait(lock);

    glog.is(DEBUG1) && glog << "ZeroMQService::merge_cfg received write lock" << std::endl;

    
    for(int i = 0, n = cfg->socket_size(); i < n; ++i)
    {
        if(!sockets_.count(cfg->socket(i).socket_id()))
        {
            // TODO (tes) - check for compatible socket type
            std::shared_ptr<zmq::socket_t> new_socket(
                new zmq::socket_t(*context_, socket_type(cfg->socket(i).socket_type())));
            
            sockets_.insert(std::make_pair(cfg->socket(i).socket_id(), ZeroMQSocket(new_socket, cfg->socket(i).receive_queue_size())));
            
            //  Initialize poll set
            zmq::pollitem_t item = { (void*)*new_socket, 0, ZMQ_POLLIN, 0 };

            // publish sockets can't receive
            if(cfg->socket(i).socket_type() != common::protobuf::ZeroMQServiceConfig::Socket::PUBLISH)
            {
                int socket_id = cfg->socket(i).socket_id();
                poll_items_.push_back(item);
                poll_item_to_socket_.insert(std::make_pair(poll_items_.size()-1, socket_id));
            }
        }

        std::shared_ptr<zmq::socket_t> this_socket = sockets_.at(cfg->socket(i).socket_id()).socket();

        int send_hwm = cfg->socket(i).send_queue_size();
        int receive_hwm = cfg->socket(i).receive_queue_size();
        this_socket->setsockopt(ZMQ_SNDHWM, &send_hwm, sizeof(send_hwm));
        this_socket->setsockopt(ZMQ_RCVHWM, &receive_hwm, sizeof(receive_hwm));
        
        if(cfg->socket(i).connect_or_bind() == common::protobuf::ZeroMQServiceConfig::Socket::CONNECT)
        {
            std::string endpoint;
            switch(cfg->socket(i).transport())
            {
                case common::protobuf::ZeroMQServiceConfig::Socket::INPROC:
                    endpoint = "inproc://" + cfg->socket(i).socket_name();
                    break;
                    
                case common::protobuf::ZeroMQServiceConfig::Socket::IPC:
                    endpoint = "ipc://" + cfg->socket(i).socket_name();
                    break;
                    
                case common::protobuf::ZeroMQServiceConfig::Socket::TCP:
                    endpoint = "tcp://" + cfg->socket(i).ethernet_address() + ":"
                        + std::to_string(cfg->socket(i).ethernet_port());
                    break;
                    
                case common::protobuf::ZeroMQServiceConfig::Socket::PGM:
                    endpoint = "pgm://" + cfg->socket(i).ethernet_address() + ";"
                        + cfg->socket(i).multicast_address() + ":" + std::to_string(cfg->socket(i).ethernet_port());
                break;
                    
                case common::protobuf::ZeroMQServiceConfig::Socket::EPGM:
                    endpoint = "epgm://" + cfg->socket(i).ethernet_address() + ";"
                        + cfg->socket(i).multicast_address() + ":" + std::to_string(cfg->socket(i).ethernet_port());
                    break;
            }

            try
            {
                this_socket->connect(endpoint.c_str());
                glog.is(DEBUG1) &&
                    glog << group(glog_out_group())
                         << cfg->socket(i).ShortDebugString()
                         << " connected to endpoint - " << endpoint
                         << std::endl;
            }    
            catch(std::exception& e)
            {
                std::stringstream ess;
                ess << "cannot connect to: " << endpoint << ": " << e.what();
                throw(goby::Exception(ess.str()));
            }
        }
        else if(cfg->socket(i).connect_or_bind() == common::protobuf::ZeroMQServiceConfig::Socket::BIND)
        {
            std::string endpoint;
            switch(cfg->socket(i).transport())
            {
                case common::protobuf::ZeroMQServiceConfig::Socket::INPROC:
                    endpoint = "inproc://" + cfg->socket(i).socket_name();
                    break;
                    
                case common::protobuf::ZeroMQServiceConfig::Socket::IPC:
                    endpoint = "ipc://" + cfg->socket(i).socket_name();
                    break;
                    
                case common::protobuf::ZeroMQServiceConfig::Socket::TCP:
                    endpoint = "tcp://*:" + std::to_string(cfg->socket(i).ethernet_port());
                    break;
                    
                case common::protobuf::ZeroMQServiceConfig::Socket::PGM:
                    throw(goby::Exception("Cannot BIND to PGM socket (use CONNECT)"));
                    break;
                    
                case common::protobuf::ZeroMQServiceConfig::Socket::EPGM:
                    throw(goby::Exception("Cannot BIND to EPGM socket (use CONNECT)"));
                    break;
            }            

            try
            {
                this_socket->bind(endpoint.c_str());

                size_t last_endpoint_size = 100;
                char last_endpoint[last_endpoint_size];
                int rc = zmq_getsockopt ((void*)*this_socket, ZMQ_LAST_ENDPOINT, &last_endpoint, &last_endpoint_size);
                
                if(rc != 0)
                    throw(std::runtime_error("Could not retrieve ZMQ_LAST_ENDPOINT"));

                if(cfg->socket(i).transport() == common::protobuf::ZeroMQServiceConfig::Socket::TCP &&
                   cfg->socket(i).ethernet_port() == 0)
                {
                    std::string last_ep(last_endpoint);
                    cfg->mutable_socket(i)->set_ethernet_port(std::stoi(last_ep.substr(last_ep.find_last_of(":")+1)));
                }
                
                
                glog.is(DEBUG1) &&
                    glog << group(glog_out_group())
                         << "bound to endpoint - " << last_endpoint
                         << ", Socket: " << cfg->socket(i).ShortDebugString()
                         << std::endl ;
            }    
            catch(std::exception& e)
            {
                std::stringstream ess;
                ess << "cannot bind to: " << endpoint << ": " << e.what();
                throw(goby::Exception(ess.str()));
            }

        }
    }
    glog.is(DEBUG1) && glog << "Leaving ZeroMQService::merge_cfg" << std::endl;
}

goby::middleware::ZeroMQService::~ZeroMQService()
{
}

goby::middleware::ZeroMQSocket& goby::middleware::ZeroMQService::socket_from_id(int socket_id)
{
    mutex_.lock();
    ReaderRegister<decltype(writer_cv_)> r(reader_count_, writer_cv_);
    mutex_.unlock();

    
    auto it = sockets_.find(socket_id);
    if(it != sockets_.end())
        return it->second;
    else
        throw(goby::Exception("Attempted to access socket_id " + std::to_string(socket_id) + " which does not exist"));
}

void goby::middleware::ZeroMQService::subscribe_all(int socket_id)
{
    auto& socket = socket_from_id(socket_id);
    std::lock_guard<std::mutex> lock(socket.mutex());
    socket.socket()->setsockopt(ZMQ_SUBSCRIBE, 0, 0);
}

void goby::middleware::ZeroMQService::unsubscribe_all(int socket_id)
{
    auto& socket = socket_from_id(socket_id);
    std::lock_guard<std::mutex> lock(socket.mutex());
    socket.socket()->setsockopt(ZMQ_UNSUBSCRIBE, 0, 0);
}

void goby::middleware::ZeroMQService::subscribe(const std::string& identifier,
                                             int socket_id)
{
    std::string zmq_filter = identifier;
    auto& socket = socket_from_id(socket_id);
    std::lock_guard<std::mutex> lock(socket.mutex());
    socket.socket()->setsockopt(ZMQ_SUBSCRIBE, zmq_filter.c_str(), zmq_filter.size());
    
    glog.is(DEBUG1) &&
        glog << group(glog_in_group())
             << "subscribed with identifier: [" << identifier << "] using zmq_filter: "
             << goby::util::hex_encode(zmq_filter) << std::endl ;
}

void goby::middleware::ZeroMQService::unsubscribe(const std::string& identifier, int socket_id)
{
    std::string zmq_filter = identifier;
    auto& socket = socket_from_id(socket_id);
    std::lock_guard<std::mutex> lock(socket.mutex());
    socket.socket()->setsockopt(ZMQ_UNSUBSCRIBE, zmq_filter.c_str(), zmq_filter.size());
    
    glog.is(DEBUG1) &&
        glog << group(glog_in_group())
             << "unsubscribed with identifier: [" << identifier << "] using zmq_filter: "
             << goby::util::hex_encode(zmq_filter) << std::endl ;        
}


void goby::middleware::ZeroMQService::send(zmq::message_t& msg,
                                        int socket_id)
{
    auto& socket = socket_from_id(socket_id);
    std::lock_guard<std::mutex> lock(socket.mutex());
    socket.socket()->send(msg);
}


int goby::middleware::ZeroMQService::poll(long timeout /* = -1 */)
{
    glog.is(DEBUG1) && glog << "Entering ZeroMQService::poll" << std::endl;
    
    mutex_.lock();
    ReaderRegister<decltype(writer_cv_)> r(reader_count_, writer_cv_);
    mutex_.unlock();

    glog.is(DEBUG1) && glog << "ZeroMQService::poll received read lock" << std::endl;
    
    int had_events = 0;
    {
        for(int i = 0, n = poll_items_.size(); i < n; ++i)
            socket_from_id(poll_item_to_socket_.at(i)).mutex().lock();
        
        long zmq_timeout = (timeout == -1) ? -1 : timeout/ZMQ_POLL_DIVISOR;
        zmq::poll (&poll_items_[0], poll_items_.size(), zmq_timeout);
        
        for(int i = 0, n = poll_items_.size(); i < n; ++i)
            socket_from_id(poll_item_to_socket_.at(i)).mutex().unlock();
    }
    
    
    for(int i = 0, n = poll_items_.size(); i < n; ++i)
    {
        if (poll_items_[i].revents & ZMQ_POLLIN) 
        {
            zmq::message_t zmq_msg;

            auto& socket = socket_from_id(poll_item_to_socket_.at(i));
            std::lock_guard<std::mutex> lock(socket.mutex());
            /* Block until a message is available to be received from socket */
            if(socket.socket()->recv(&zmq_msg))
            {
                glog.is(DEBUG3) &&
                    glog << group(glog_in_group())
                         << "Had event for poll item " << i << std::endl ;
                socket.buffer().push_back(std::vector<char>(reinterpret_cast<char*>(zmq_msg.data()), reinterpret_cast<char*>(zmq_msg.data())+zmq_msg.size()));
            }
            else
            {
                glog.is(DEBUG1) &&
                    glog << warn << "zmq_recv failed" << std::endl;
                continue;
            }
            ++had_events;
        }
    }

    
    glog.is(DEBUG1) && glog << "Leaving ZeroMQService::poll" << std::endl;
    return had_events;
}



// static 
int goby::middleware::ZeroMQService::socket_type(common::protobuf::ZeroMQServiceConfig::Socket::SocketType type)
{
    switch(type)
    {
        case common::protobuf::ZeroMQServiceConfig::Socket::PUBLISH: return ZMQ_PUB;
        case common::protobuf::ZeroMQServiceConfig::Socket::SUBSCRIBE: return ZMQ_SUB;
        case common::protobuf::ZeroMQServiceConfig::Socket::REPLY: return ZMQ_REP;
        case common::protobuf::ZeroMQServiceConfig::Socket::REQUEST: return ZMQ_REQ;
//        case common::protobuf::ZeroMQServiceConfig::Socket::ZMQ_PUSH: return ZMQ_PUSH;
//        case common::protobuf::ZeroMQServiceConfig::Socket::ZMQ_PULL: return ZMQ_PULL;
//        case common::protobuf::ZeroMQServiceConfig::Socket::ZMQ_DEALER: return ZMQ_DEALER;
//        case common::protobuf::ZeroMQServiceConfig::Socket::ZMQ_ROUTER: return ZMQ_ROUTER;
    }
    throw(goby::Exception("Invalid SocketType"));
}
