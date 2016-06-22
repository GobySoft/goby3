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
#include "zeromq_packet.h"
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

goby::common::ZeroMQService::ZeroMQService(boost::shared_ptr<zmq::context_t> context)
    : context_(context)
{
    init();
}

goby::common::ZeroMQService::ZeroMQService()
    : context_(new zmq::context_t(2))
{
    init();
}

void goby::common::ZeroMQService::init()
{
    glog.add_group(glog_out_group(), common::Colors::lt_magenta);
    glog.add_group(glog_in_group(), common::Colors::lt_blue);
}

void goby::common::ZeroMQService::process_cfg(protobuf::ZeroMQServiceConfig& cfg)
{
    for(int i = 0, n = cfg.socket_size(); i < n; ++i)
    {
        if(!sockets_.count(cfg.socket(i).socket_id()))
        {
            // TODO (tes) - check for compatible socket type
            boost::shared_ptr<zmq::socket_t> new_socket(
                new zmq::socket_t(*context_, socket_type(cfg.socket(i).socket_type())));
            
            sockets_.insert(std::make_pair(cfg.socket(i).socket_id(), ZeroMQSocket(new_socket)));
            
            //  Initialize poll set
            zmq::pollitem_t item = { *new_socket, 0, ZMQ_POLLIN, 0 };

            // publish sockets can't receive
            if(cfg.socket(i).socket_type() != protobuf::ZeroMQServiceConfig::Socket::PUBLISH)
            {
                register_poll_item(item,
                                   boost::bind(&goby::common::ZeroMQService::handle_receive,
                                               this, _1, _2, _3, cfg.socket(i).socket_id()));
            }
        }

        boost::shared_ptr<zmq::socket_t> this_socket = socket_from_id(cfg.socket(i).socket_id()).socket();
        
        if(cfg.socket(i).connect_or_bind() == protobuf::ZeroMQServiceConfig::Socket::CONNECT)
        {
            std::string endpoint;
            switch(cfg.socket(i).transport())
            {
                case protobuf::ZeroMQServiceConfig::Socket::INPROC:
                    endpoint = "inproc://" + cfg.socket(i).socket_name();
                    break;
                    
                case protobuf::ZeroMQServiceConfig::Socket::IPC:
                    endpoint = "ipc://" + cfg.socket(i).socket_name();
                    break;
                    
                case protobuf::ZeroMQServiceConfig::Socket::TCP:
                    endpoint = "tcp://" + cfg.socket(i).ethernet_address() + ":"
                        + as<std::string>(cfg.socket(i).ethernet_port());
                    break;
                    
                case protobuf::ZeroMQServiceConfig::Socket::PGM:
                    endpoint = "pgm://" + cfg.socket(i).ethernet_address() + ";"
                        + cfg.socket(i).multicast_address() + ":" + as<std::string>(cfg.socket(i).ethernet_port());
                break;
                    
                case protobuf::ZeroMQServiceConfig::Socket::EPGM:
                    endpoint = "epgm://" + cfg.socket(i).ethernet_address() + ";"
                        + cfg.socket(i).multicast_address() + ":" + as<std::string>(cfg.socket(i).ethernet_port());
                    break;
            }

            try
            {
                this_socket->connect(endpoint.c_str());
                glog.is(DEBUG1) &&
                    glog << group(glog_out_group())
                         << cfg.socket(i).ShortDebugString()
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
        else if(cfg.socket(i).connect_or_bind() == protobuf::ZeroMQServiceConfig::Socket::BIND)
        {
            std::string endpoint;
            switch(cfg.socket(i).transport())
            {
                case protobuf::ZeroMQServiceConfig::Socket::INPROC:
                    endpoint = "inproc://" + cfg.socket(i).socket_name();
                    break;
                    
                case protobuf::ZeroMQServiceConfig::Socket::IPC:
                    endpoint = "ipc://" + cfg.socket(i).socket_name();
                    break;
                    
                case protobuf::ZeroMQServiceConfig::Socket::TCP:
                    endpoint = "tcp://*:" + as<std::string>(cfg.socket(i).ethernet_port());
                    break;
                    
                case protobuf::ZeroMQServiceConfig::Socket::PGM:
                    throw(goby::Exception("Cannot BIND to PGM socket (use CONNECT)"));
                    break;
                    
                case protobuf::ZeroMQServiceConfig::Socket::EPGM:
                    throw(goby::Exception("Cannot BIND to EPGM socket (use CONNECT)"));
                    break;
            }            

            try
            {
                this_socket->bind(endpoint.c_str());

                size_t last_endpoint_size = 100;
                char last_endpoint[last_endpoint_size];
                int rc = zmq_getsockopt (*this_socket, ZMQ_LAST_ENDPOINT, &last_endpoint, &last_endpoint_size);

                if(rc != 0)
                    throw(std::runtime_error("Could not retrieve ZMQ_LAST_ENDPOINT"));

                if(cfg.socket(i).transport() == protobuf::ZeroMQServiceConfig::Socket::TCP &&
                   cfg.socket(i).ethernet_port() == 0)
                {
                    std::string last_ep(last_endpoint);
                    cfg.mutable_socket(i)->set_ethernet_port(std::stoi(last_ep.substr(last_ep.find_last_of(":")+1)));
                }
                
                
                glog.is(DEBUG1) &&
                    glog << group(glog_out_group())
                         << "bound to endpoint - " << last_endpoint
                         << ", Socket: " << cfg.socket(i).ShortDebugString()
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
}

goby::common::ZeroMQService::~ZeroMQService()
{
//    std::cout << "ZeroMQService: " << this << ": destroyed" << std::endl;
//    std::cout << "poll_mutex " << &poll_mutex_ << std::endl;
}

int goby::common::ZeroMQService::socket_type(protobuf::ZeroMQServiceConfig::Socket::SocketType type)
{
    switch(type)
    {
        case protobuf::ZeroMQServiceConfig::Socket::PUBLISH: return ZMQ_PUB;
        case protobuf::ZeroMQServiceConfig::Socket::SUBSCRIBE: return ZMQ_SUB;
        case protobuf::ZeroMQServiceConfig::Socket::REPLY: return ZMQ_REP;
        case protobuf::ZeroMQServiceConfig::Socket::REQUEST: return ZMQ_REQ;
//        case protobuf::ZeroMQServiceConfig::Socket::ZMQ_PUSH: return ZMQ_PUSH;
//        case protobuf::ZeroMQServiceConfig::Socket::ZMQ_PULL: return ZMQ_PULL;
//        case protobuf::ZeroMQServiceConfig::Socket::ZMQ_DEALER: return ZMQ_DEALER;
//        case protobuf::ZeroMQServiceConfig::Socket::ZMQ_ROUTER: return ZMQ_ROUTER;
    }
    throw(goby::Exception("Invalid SocketType"));
}

goby::common::ZeroMQSocket& goby::common::ZeroMQService::socket_from_id(int socket_id)
{
    std::map<int, ZeroMQSocket >::iterator it = sockets_.find(socket_id);
    if(it != sockets_.end())
        return it->second;
    else
        throw(goby::Exception("Attempted to access socket_id " + as<std::string>(socket_id) + " which does not exist"));
}

void goby::common::ZeroMQService::subscribe_all(int socket_id)
{
    socket_from_id(socket_id).socket()->setsockopt(ZMQ_SUBSCRIBE, 0, 0);
}

void goby::common::ZeroMQService::unsubscribe_all(int socket_id)
{
    socket_from_id(socket_id).socket()->setsockopt(ZMQ_UNSUBSCRIBE, 0, 0);
}

void goby::common::ZeroMQService::subscribe(int marshalling_scheme,
                                       const std::string& identifier,
                                       int socket_id)
{
    pre_subscribe_hooks(marshalling_scheme, identifier, socket_id);
    
    std::string zmq_filter = zeromq_packet_make_header(marshalling_scheme, identifier);
    int NULL_TERMINATOR_SIZE = 1;
    zmq_filter.resize(zmq_filter.size() - NULL_TERMINATOR_SIZE);
    socket_from_id(socket_id).socket()->setsockopt(ZMQ_SUBSCRIBE, zmq_filter.c_str(), zmq_filter.size());
    
    glog.is(DEBUG1) &&
        glog << group(glog_in_group())
             << "subscribed for marshalling " << marshalling_scheme
             << " with identifier: [" << identifier << "] using zmq_filter: "
             << goby::util::hex_encode(zmq_filter) << std::endl ;

        
    post_subscribe_hooks(marshalling_scheme, identifier, socket_id);
}

void goby::common::ZeroMQService::unsubscribe(int marshalling_scheme,
                                       const std::string& identifier,
                                       int socket_id)
{
    std::string zmq_filter = zeromq_packet_make_header(marshalling_scheme, identifier);
    int NULL_TERMINATOR_SIZE = 1;
    zmq_filter.resize(zmq_filter.size() - NULL_TERMINATOR_SIZE);
    socket_from_id(socket_id).socket()->setsockopt(ZMQ_UNSUBSCRIBE, zmq_filter.c_str(), zmq_filter.size());
    
    glog.is(DEBUG1) &&
        glog << group(glog_in_group())
             << "unsubscribed for marshalling " << marshalling_scheme
             << " with identifier: [" << identifier << "] using zmq_filter: "
             << goby::util::hex_encode(zmq_filter) << std::endl ;        
}


void goby::common::ZeroMQService::send(int marshalling_scheme,
                                       const std::string& identifier,
                                       const std::string& body,
                                       int socket_id)
{
    pre_send_hooks(marshalling_scheme, identifier, socket_id);
    
    std::string raw;
    zeromq_packet_encode(&raw, marshalling_scheme, identifier, body);
    
    zmq::message_t msg(raw.size());
    memcpy(msg.data(), raw.c_str(), raw.size()); // insert packet

    glog.is(DEBUG3) &&
        glog << group(glog_out_group())
             << "Sent message (hex): " << hex_encode(std::string(static_cast<const char*>(msg.data()),msg.size()))
             << std::endl ;
    socket_from_id(socket_id).socket()->send(msg);

    post_send_hooks(marshalling_scheme, identifier, socket_id);
}


void goby::common::ZeroMQService::handle_receive(const void* data,
                                            int size,
                                            int message_part,
                                            int socket_id)
{
    std::string bytes(static_cast<const char*>(data),
                      size);
    

    glog.is(DEBUG3) &&
        glog << group(glog_in_group())
             << "Received message (hex): " << goby::util::hex_encode(bytes)
             << std::endl ;
    
    
    int marshalling_scheme = MARSHALLING_UNKNOWN;
    std::string identifier;
    std::string body;
    
    switch(message_part)
    {
        case 0:
        {
            zeromq_packet_decode(bytes, &marshalling_scheme, &identifier, &body);

            glog.is(DEBUG3) &&
                glog << group(glog_in_group())
                     << "Received message of type: [" << identifier << "]" << std::endl;

            glog.is(DEBUG3) &&
                glog << group(glog_in_group())
                     << "Body [" << goby::util::hex_encode(body)<< "]" << std::endl;

            
            if(socket_from_id(socket_id).check_blackout(marshalling_scheme, identifier))
            {
                inbox_signal_(marshalling_scheme,
                              identifier,
                              body,
                              socket_id);
            }
        }
        break;
            
            
        default:
            throw(std::runtime_error("Got more parts to the message than expecting (expecting only 1)"));    
            break;
    }
}

bool goby::common::ZeroMQService::poll(long timeout /* = -1 */)
{
    boost::mutex::scoped_lock slock(poll_mutex_);

//    glog.is(DEBUG2) && glog << "Have " << poll_items_.size() << " items to poll" << std::endl ;   
    bool had_events = false;
    zmq::poll (&poll_items_[0], poll_items_.size(), timeout/ZMQ_POLL_DIVISOR);
    for(int i = 0, n = poll_items_.size(); i < n; ++i)
    {
        if (poll_items_[i].revents & ZMQ_POLLIN) 
        {
            int message_part = 0;
            more_t more;
            size_t more_size = sizeof(more_t);
            do {
                /* Create an empty ØMQ message to hold the message part */
                zmq_msg_t part;
                int rc = zmq_msg_init (&part);
                
                if(rc == -1)
                {
                    glog.is(DEBUG1) &&
                        glog << warn << "zmq_msg_init failed" << std::endl ;
                    continue;
                }
                
                /* Block until a message is available to be received from socket */
                rc = zmq_msg_recv (&part, poll_items_[i].socket, 0);
                glog.is(DEBUG3) &&
                    glog << group(glog_in_group())
                         << "Had event for poll item " << i << std::endl ;
                poll_callbacks_[i](zmq_msg_data(&part), zmq_msg_size(&part), message_part);

                if(rc == -1)
                {
                    glog.is(DEBUG1) &&
                        glog << warn << "zmq_recv failed" << std::endl ;
                    continue;
                }
                
                /* Determine if more message parts are to follow */
                rc = zmq_getsockopt (poll_items_[i].socket, ZMQ_RCVMORE, &more, &more_size);

                if(rc == -1)
                {
                    glog.is(DEBUG1) &&
                        glog << warn << "zmq_getsocketopt failed" << std::endl ;
                    continue;
                }

                zmq_msg_close (&part);
                ++message_part;
            } while (more);
            had_events = true;
        }
    }
    return had_events;
}




void goby::common::ZeroMQSocket::set_global_blackout(boost::posix_time::time_duration duration)
{
    glog.is(DEBUG2) &&
        glog << group(ZeroMQService::glog_in_group())
             << "Global blackout set to " << duration << std::endl ;
    global_blackout_ = duration;
    global_blackout_set_ = true;
}


void goby::common::ZeroMQSocket::set_blackout(int marshalling_scheme,
                                            const std::string& identifier,
                                            boost::posix_time::time_duration duration)            
{
    glog.is(DEBUG2) &&
        glog << group(ZeroMQService::glog_in_group())
             << "Blackout for marshalling scheme: " << marshalling_scheme << " and identifier " << identifier << " set to " << duration << std::endl ;
    blackout_info_[std::make_pair(marshalling_scheme, identifier)] = BlackoutInfo(duration);
    local_blackout_set_ = true;
}

bool goby::common::ZeroMQSocket::check_blackout(int marshalling_scheme,
                                              const std::string& identifier)
{
    if(!local_blackout_set_ && !global_blackout_set_)
    {
        return true;
    }
    else
    {
        boost::posix_time::ptime this_time = goby::common::goby_time();

        BlackoutInfo& blackout_info = blackout_info_[std::make_pair(marshalling_scheme, identifier)];
        
        const boost::posix_time::ptime& last_post_time =
            blackout_info.last_post_time;
        
        if((local_blackout_set_ && // local blackout
            this_time - last_post_time > blackout_info.blackout_interval))
        {
            blackout_info.last_post_time = this_time;
            return true;
        }
        else if((global_blackout_set_ && // global blackout
                 this_time - last_post_time > global_blackout_)) // fails global blackout
        {
            blackout_info.last_post_time = this_time;
            return true;
        }
        else
        {
            glog.is(DEBUG3) && 
                glog << group(ZeroMQService::glog_in_group())
                     << "Message (marshalling scheme: " << marshalling_scheme
                     << ", identifier: " << identifier << ")" << " is in blackout: this time:"
                     << this_time << ", last time: " << last_post_time << ", global blackout: "
                     << global_blackout_ << ", local blackout: " << blackout_info.blackout_interval
                     << ", difference last and this: " << this_time - last_post_time
                     << std::endl ;
            return false;
        }
    }    
}
