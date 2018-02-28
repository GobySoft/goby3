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

#ifndef ZEROMQNODE20110413H
#define ZEROMQNODE20110413H

#include <iostream>
#include <string>
#include <boost/bind.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/function.hpp>
#include <boost/signals2.hpp>

#include "goby/common/protobuf/zero_mq_node_config.pb.h"

#include <zmq.hpp>

#include "core_constants.h"
#include "goby/common/logger.h"

namespace goby
{
    namespace common
    {
        class ZeroMQSocket
        {
          public:
          ZeroMQSocket()
              : global_blackout_(boost::posix_time::not_a_date_time),
                local_blackout_set_(false),
                global_blackout_set_(false)
           { }

          ZeroMQSocket(boost::shared_ptr<zmq::socket_t> socket)
              : socket_(socket),
                global_blackout_(boost::posix_time::not_a_date_time),
                local_blackout_set_(false),
                global_blackout_set_(false)
                { }

            void set_global_blackout(boost::posix_time::time_duration duration);
            void set_blackout(int marshalling_scheme,
                              const std::string& identifier,
                              boost::posix_time::time_duration duration);

            void clear_blackout(int marshalling_scheme,
                                const std::string& identifier)
            {
                blackout_info_.erase(std::make_pair(marshalling_scheme, identifier));
                local_blackout_set_ = false;
            } 
            void clear_global_blackout()
            {
                global_blackout_ = boost::posix_time::not_a_date_time;
                global_blackout_set_ = false;
            }   

            // true means go ahead and post this message
            // false means in blackout
            bool check_blackout(int marshalling_scheme,
                                const std::string& identifier);
            
            

            void set_socket(boost::shared_ptr<zmq::socket_t> socket)
            { socket_ = socket; }
            
            boost::shared_ptr<zmq::socket_t>& socket()
            { return socket_; }            
            
          private:
            struct BlackoutInfo
            {
            BlackoutInfo(boost::posix_time::time_duration interval =
                         boost::posix_time::not_a_date_time)
                : blackout_interval(interval),
                    last_post_time(boost::posix_time::neg_infin)
                    { }
                
                boost::posix_time::time_duration blackout_interval;
                boost::posix_time::ptime last_post_time;
            };

            boost::shared_ptr<zmq::socket_t> socket_;

            boost::posix_time::time_duration global_blackout_;
            bool local_blackout_set_;
            bool global_blackout_set_;
            std::map<std::pair<int, std::string>, BlackoutInfo> blackout_info_;
        };

        
        class ZeroMQService
        {
          public:
            ZeroMQService();
            ZeroMQService(boost::shared_ptr<zmq::context_t> context);
            virtual ~ZeroMQService();
            

            void set_cfg(protobuf::ZeroMQServiceConfig* cfg)
            {
                process_cfg(*cfg);
                cfg_.CopyFrom(*cfg);
            }
            
            void merge_cfg(protobuf::ZeroMQServiceConfig* cfg)
            {
                process_cfg(*cfg);
                cfg_.MergeFrom(*cfg);
            }

            void set_cfg(const protobuf::ZeroMQServiceConfig& orig_cfg)
            {
                protobuf::ZeroMQServiceConfig cfg(orig_cfg);
                set_cfg(&cfg);
            }
            
            void merge_cfg(const protobuf::ZeroMQServiceConfig& orig_cfg)
            {
                protobuf::ZeroMQServiceConfig cfg(orig_cfg);
                merge_cfg(&cfg);
            }
            
            void subscribe_all(int socket_id);
            void unsubscribe_all(int socket_id);            
            
            void send(int marshalling_scheme,
                      const std::string& identifier,
                      const std::string& body,
                      int socket_id);
            
            void subscribe(int marshalling_scheme,
                           const std::string& identifier,
                           int socket_id);

            void unsubscribe(int marshalling_scheme,
                           const std::string& identifier,
                           int socket_id);
            
            template<class C>
                void connect_inbox_slot(
                    void(C::*mem_func)(int,
                                       const std::string&,
                                       const std::string&,
                                       int),
                    C* obj)
            {
                goby::glog.is(goby::common::logger::DEBUG1) &&
                    goby::glog << "ZeroMQService: made connection for: "
                               << typeid(obj).name() << std::endl;
                connect_inbox_slot(boost::bind(mem_func, obj, _1, _2, _3, _4));
            }

            
            void connect_inbox_slot(
                boost::function<void (int marshalling_scheme,
                                      const std::string& identifier,
                                      const std::string& body,
                                      int socket_id)> slot)
            { inbox_signal_.connect(slot); }

            
            int poll(long timeout = -1);
	    void close_all()
	    {
	      sockets_.clear();
	      poll_items_.clear();
	      poll_callbacks_.clear();
	    }


            ZeroMQSocket& socket_from_id(int socket_id);
            
            template<class C>
                void register_poll_item(
                    const zmq::pollitem_t& item,                    
                    void(C::*mem_func)(const void*, int, int),
                    C* obj)
            { register_poll_item(item, boost::bind(mem_func, obj, _1, _2, _3)); }
            

            void register_poll_item(
                const zmq::pollitem_t& item,
                boost::function<void (const void* data, int size, int message_part)> callback)
            {
                poll_items_.push_back(item);
                poll_callbacks_.insert(std::make_pair(poll_items_.size()-1, callback));
            }
            
            boost::shared_ptr<zmq::context_t> zmq_context() { return context_; }


            boost::signals2::signal<void (int marshalling_scheme,
                                const std::string& identifier,
                                int socket_id)> pre_send_hooks;

            boost::signals2::signal<void (int marshalling_scheme,
                                const std::string& identifier,
                                int socket_id)> pre_subscribe_hooks;

            boost::signals2::signal<void (int marshalling_scheme,
                                const std::string& identifier,
                                int socket_id)> post_send_hooks;

            boost::signals2::signal<void (int marshalling_scheme,
                                const std::string& identifier,
                                int socket_id)> post_subscribe_hooks;

            static std::string glog_out_group() { return "goby::common::zmq::out"; }
            static std::string glog_in_group() { return "goby::common::zmq::in"; }
            
            friend class ZeroMQSocket;
          private:
            ZeroMQService(const ZeroMQService&);
            ZeroMQService& operator= (const ZeroMQService&);
            
            void init();
            
            void process_cfg(protobuf::ZeroMQServiceConfig& cfg);

            void handle_receive(const void* data, int size, int message_part, int socket_id);

            int socket_type(protobuf::ZeroMQServiceConfig::Socket::SocketType type);

          private:
            boost::shared_ptr<zmq::context_t> context_;
            std::map<int, ZeroMQSocket > sockets_;
            std::vector<zmq::pollitem_t> poll_items_;

            protobuf::ZeroMQServiceConfig cfg_;
            
            // maps poll_items_ index to a callback function
            std::map<size_t, boost::function<void (const void* data, int size, int message_part)> > poll_callbacks_;
            
            boost::signals2::signal<void (int marshalling_scheme,
                                          const std::string& identifier,
                                          const std::string& body,
                                          int socket_id)> inbox_signal_;
            boost::mutex poll_mutex_;
        };
    }
}


#endif
