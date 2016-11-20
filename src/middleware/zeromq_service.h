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

#ifndef ZEROMQSERVICE20160817H
#define ZEROMQSERVICE20160817H

#include <iostream>
#include <string>
#include <unordered_map>

#include <boost/thread/mutex.hpp>

#include "goby/common/protobuf/zero_mq_node_config.pb.h"

#include <zmq.hpp>

#include "goby/common/core_constants.h"
#include "goby/common/logger.h"

namespace goby
{
    namespace middleware
    {
        class ZeroMQSocket
        {
          public:
          ZeroMQSocket() { }

          ZeroMQSocket(std::shared_ptr<zmq::socket_t> socket)
              : socket_(socket)  { }

            void set_socket(std::shared_ptr<zmq::socket_t> socket)
            { socket_ = socket; }
            
            std::shared_ptr<zmq::socket_t>& socket()
            { return socket_; }            
            
          private:
            std::shared_ptr<zmq::socket_t> socket_;
        };

        
        class ZeroMQService
        {
          public:
            ZeroMQService();
            ZeroMQService(std::shared_ptr<zmq::context_t> context);
            virtual ~ZeroMQService();
            

            void set_cfg(common::protobuf::ZeroMQServiceConfig* cfg)
            {
                process_cfg(*cfg);
                cfg_.CopyFrom(*cfg);
            }
            
            void merge_cfg(common::protobuf::ZeroMQServiceConfig* cfg)
            {
                process_cfg(*cfg);
                cfg_.MergeFrom(*cfg);
            }

            void set_cfg(const common::protobuf::ZeroMQServiceConfig& orig_cfg)
            {
                common::protobuf::ZeroMQServiceConfig cfg(orig_cfg);
                set_cfg(&cfg);
            }
            
            void merge_cfg(const common::protobuf::ZeroMQServiceConfig& orig_cfg)
            {
                common::protobuf::ZeroMQServiceConfig cfg(orig_cfg);
                merge_cfg(&cfg);
            }
            
            void subscribe_all(int socket_id);
            void unsubscribe_all(int socket_id);            
            
            void send(zmq::message_t& msg,
                      int socket_id);
            
            void subscribe(const std::string& identifier,
                           int socket_id);

            void unsubscribe(const std::string& identifier,
                             int socket_id);            
            
            int poll(long timeout = -1);
	    void close_all()
	    {
	      sockets_.clear();
	      poll_items_.clear();
	      poll_callbacks_.clear();
	    }


            ZeroMQSocket& socket_from_id(int socket_id);
            

            void register_poll_item(
                const zmq::pollitem_t& item,
                std::function<void (const void* data, int size, int message_part)> callback)
            {
                poll_items_.push_back(item);
                poll_callbacks_.insert(std::make_pair(poll_items_.size()-1, callback));
            }
            
            std::shared_ptr<zmq::context_t> zmq_context() { return context_; }
            
            static std::string glog_out_group() { return "goby::common::zmq::out"; }
            static std::string glog_in_group() { return "goby::common::zmq::in"; }

            std::function<void (const void* data, int size, int message_part, int socket_id)> receive_func;
            
            friend class ZeroMQSocket;
          private:
            ZeroMQService(const ZeroMQService&);
            ZeroMQService& operator= (const ZeroMQService&);
            
            void init();
            
            void process_cfg(common::protobuf::ZeroMQServiceConfig& cfg);

            int socket_type(common::protobuf::ZeroMQServiceConfig::Socket::SocketType type);

          private:
            std::shared_ptr<zmq::context_t> context_;
            std::unordered_map<int, ZeroMQSocket > sockets_;
            std::vector<zmq::pollitem_t> poll_items_;

            common::protobuf::ZeroMQServiceConfig cfg_;
            
            // maps poll_items_ index to a callback function
            std::map<size_t, std::function<void (const void* data, int size, int message_part)> > poll_callbacks_;            

            boost::mutex poll_mutex_;
        };
    }
}


#endif
