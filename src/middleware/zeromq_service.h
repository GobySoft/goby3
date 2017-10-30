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

#include <mutex>
#include <condition_variable>

#include <iostream>
#include <string>
#include <unordered_map>
#include <boost/circular_buffer.hpp>
#include <vector>

#include <zmq.hpp>

#include "goby/common/protobuf/zero_mq_node_config.pb.h"
#include "goby/common/core_constants.h"
#include "goby/common/logger.h"

#include "transport-interthread.h" // for ReaderRegister

namespace goby
{
    namespace middleware
    {
      void setup_socket(zmq::socket_t& socket, const goby::common::protobuf::ZeroMQServiceConfig::Socket& cfg);
        class ZeroMQSocket
        {
          public:
        ZeroMQSocket(std::shared_ptr<zmq::socket_t> socket, int buffer_size, std::mutex& mutex)
            : socket_(socket),
                buffer_(buffer_size),
                mutex_(mutex)
                { }

            void set_socket(std::shared_ptr<zmq::socket_t> socket)
            { socket_ = socket; }
            
            std::shared_ptr<zmq::socket_t>& socket()
            { return socket_; }            

            std::mutex& mutex() { return mutex_; }
            boost::circular_buffer<std::vector<char>>& buffer() { return buffer_; }
            
          private:
            std::shared_ptr<zmq::socket_t> socket_;
            boost::circular_buffer<std::vector<char>> buffer_;
            // protects socket_ and buffer_
            std::mutex& mutex_;
        };

        
        class ZeroMQService
        {
          public:
            ZeroMQService();
            ZeroMQService(std::shared_ptr<zmq::context_t> context);
            virtual ~ZeroMQService();
            

            void merge_cfg(common::protobuf::ZeroMQServiceConfig* cfg);

            void subscribe_all(int socket_id);
            void unsubscribe_all(int socket_id);            
            
            void send(zmq::message_t& msg,
                      int socket_id);
            
            void subscribe(const std::string& identifier,
                           int socket_id);

            void unsubscribe(const std::string& identifier,
                             int socket_id);            
            
            int poll(long timeout);

            ZeroMQSocket& socket_from_id(int socket_id);
            
                
            std::shared_ptr<zmq::context_t> zmq_context() { return context_; }
            
            static std::string glog_out_group() { return "goby::common::zmq::out"; }
            static std::string glog_in_group() { return "goby::common::zmq::in"; }

            
            friend class ZeroMQSocket;
          private:
            ZeroMQService(const ZeroMQService&);
            ZeroMQService& operator= (const ZeroMQService&);
            
            static int socket_type(common::protobuf::ZeroMQServiceConfig::Socket::SocketType type);

          private:
            std::shared_ptr<zmq::context_t> context_;
            
            std::unordered_map<int, ZeroMQSocket > sockets_;
            std::vector<zmq::pollitem_t> poll_items_;
            // maps poll_items_ index to sockets_ index
            std::map<size_t, int> poll_item_to_socket_;
            
            // protects sockets_, poll_items_ and poll_item_to_socket_
            std::mutex mutex_;
            std::condition_variable writer_cv_;
            std::atomic<int> reader_count_;

            // protects writeable sockets
            std::mutex socket_write_mutex_;
            // protects readable sockets
            std::mutex socket_read_mutex_;
        };
    }
}


#endif
