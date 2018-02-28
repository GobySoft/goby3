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

#ifndef PROTOBUFNODE20110418H
#define PROTOBUFNODE20110418H

#include <boost/bind.hpp>
#include <boost/function.hpp>
#include <boost/unordered_map.hpp>

#include "goby/common/logger.h"
#include "goby/util/dynamic_protobuf_manager.h"
#include "goby/common/core_helpers.h"

#include "goby/common/node_interface.h"
#include "subscription.h"

namespace goby
{
    namespace pb
    {
        class ProtobufNode : public goby::common::NodeInterface<google::protobuf::Message>
        {

          protected:
          ProtobufNode(common::ZeroMQService* service)
              : common::NodeInterface<google::protobuf::Message>(service)
            { }
            
            virtual ~ProtobufNode()
            { }


            virtual void protobuf_inbox(const std::string& protobuf_type_name,
                                        const std::string& body,
                                        int socket_id,
                                        const std::string& group) = 0;

            void send(const google::protobuf::Message& msg, int socket_id, const std::string& group = "");
            void subscribe(const std::string& identifier, int socket_id);
            void subscribe(const std::string& protobuf_type_name, int socket_id, const std::string& group);

            
          private:
            void inbox(int marshalling_scheme,
                       const std::string& identifier,
                       const std::string& body,
                       int socket_id);
            

        };

        class StaticProtobufNode : public ProtobufNode
        {
          public:
          StaticProtobufNode(common::ZeroMQService* service)
                : ProtobufNode(service)
            { }
                        
            virtual ~StaticProtobufNode()
            { }
            
            
            /// \brief Subscribe to a message (of any type derived from google::protobuf::Message)            
            ///
            /// \param socket_id Unique id assigned to the SUBSCRIBE socket that you want to subscribe to
            /// \param handler Function object to be called as soon as possible upon receipt of a message of this type. The signature of `handler` must match: void handler(const ProtoBufMessage& msg). if `handler` is omitted, no handler is called and only the newest message buffer is updated upon message receipt (for calls to newest<ProtoBufMessage>())
             template<typename ProtoBufMessage>
                void subscribe(
                    int socket_id,
                    boost::function<void (const ProtoBufMessage&)> handler =
                    boost::function<void (const ProtoBufMessage&)>(),
                    const std::string& group = ""
                    );

            template<typename ProtoBufMessage, class C>
                void subscribe(int socket_id,
                               void(C::*mem_func)(const ProtoBufMessage&),
                               C* obj,
                               const std::string& group = "")
            { subscribe<ProtoBufMessage>(socket_id, boost::bind(mem_func, obj, _1), group); }

             
             template<typename ProtoBufMessage>
                 void on_receipt(
                     int socket_id,
                     boost::function<void (const ProtoBufMessage&)> handler =
                     boost::function<void (const ProtoBufMessage&)>(),
                     const std::string& group = ""
                     );

            template<typename ProtoBufMessage, class C>
                void on_receipt(int socket_id,
                                void(C::*mem_func)(const ProtoBufMessage&),
                                C* obj,
                                const std::string& group = "")
            { on_receipt<ProtoBufMessage>(socket_id, boost::bind(mem_func, obj, _1), group); }


            void send(const google::protobuf::Message& msg, int socket_id, const std::string& group = "")
            {
                ProtobufNode::send(msg, socket_id, group);
            }
             
            
            /// \name Message Accessors
            //@{
            /// \brief Fetchs the newest received message of this type 
            ///
            /// You must subscribe() for this type before using this method
//            template<typename ProtoBufMessage>
//                const ProtoBufMessage& newest() const;
            
            //@}
            

            
          private:
            void protobuf_inbox(const std::string& protobuf_type_name,
                                const std::string& body,
                                int socket_id,
                                const std::string& group);
            

          private:
            // key = type of var
            // value = Subscription object for all the subscriptions,  handler, newest message, etc.
            boost::unordered_multimap<std::string, boost::shared_ptr<SubscriptionBase> > subscriptions_;  
            
        };

        class DynamicProtobufNode : public ProtobufNode
        {
          protected:  
          DynamicProtobufNode(common::ZeroMQService* service)
                : ProtobufNode(service)
            { }
            
            virtual ~DynamicProtobufNode()
            { }

            void subscribe(int socket_id,
                           boost::function<void (boost::shared_ptr<google::protobuf::Message> msg)> handler,
                           const std::string& group);

            template<class C>
                void subscribe(int socket_id,
                               void(C::*mem_func)(boost::shared_ptr<google::protobuf::Message> msg),
                               C* obj,
                               const std::string& group)
            { subscribe(socket_id, boost::bind(mem_func, obj, _1), group); }
             
            void on_receipt(
                int socket_id,
                boost::function<void (boost::shared_ptr<google::protobuf::Message> msg)> handler,
                const std::string& group);
            
            template<class C>
                void on_receipt(int socket_id,
                                void(C::*mem_func)(boost::shared_ptr<google::protobuf::Message> msg),
                                C* obj,
                                const std::string& group)
            { on_receipt(socket_id, boost::bind(mem_func, obj, _1), group); }
            
            // virtual void dynamic_protobuf_inbox(boost::shared_ptr<google::protobuf::Message> msg, int socket_id, const std::string& group) = 0;
            
          private:
            void protobuf_inbox(const std::string& protobuf_type_name,
                                const std::string& body,
                                int socket_id,
                                const std::string& group);
          private:
            // key = type of var
            // value = Subscription object for all the subscriptions,  handler, newest message, etc.
            boost::unordered_multimap<std::string, boost::function<void (boost::shared_ptr<google::protobuf::Message> msg)> > subscriptions_;
        };
        
        
    }    
}


template<typename ProtoBufMessage>
void goby::pb::StaticProtobufNode::on_receipt(
    int socket_id,
    boost::function<void (const ProtoBufMessage&)> handler
    /*= boost::function<void (const ProtoBufMessage&)>()*/,
    const std::string& group)
{    
    using goby::glog;
    
    const std::string& protobuf_type_name = ProtoBufMessage::descriptor()->full_name();

    glog.is(goby::common::logger::DEBUG1) && 
        glog << "registering on_receipt handler for " << protobuf_type_name  << std::endl;
    
    // machinery so we can call the proper handler upon receipt of this type
    boost::shared_ptr<SubscriptionBase> subscription(
        new Subscription<ProtoBufMessage>(handler, protobuf_type_name, group));
    subscriptions_.insert(std::make_pair(protobuf_type_name, subscription));
}

template<typename ProtoBufMessage>
void goby::pb::StaticProtobufNode::subscribe(
    int socket_id,
    boost::function<void (const ProtoBufMessage&)> handler
    /*= boost::function<void (const ProtoBufMessage&)>()*/,
    const std::string& group)
{
    const std::string& protobuf_type_name = ProtoBufMessage::descriptor()->full_name();
    on_receipt(socket_id, handler, group);

    glog.is(goby::common::logger::DEBUG1) && 
        glog << "subscribing for " << protobuf_type_name  << std::endl;

    ProtobufNode::subscribe(protobuf_type_name, socket_id, group);
}

/// See goby::pb::StaticProtobufNode::newest()
/* template<typename ProtoBufMessage> */
/* const ProtoBufMessage& goby::pb::StaticProtobufNode::newest() const  */
/* { */
/*     // RTTI needed so we can store subscriptions with a common (non-template) base but also */
/*     // return the subclass requested */
/*     const std::string full_name = ProtoBufMessage::descriptor()->full_name(); */
/*     typedef boost::unordered_map<std::string, boost::shared_ptr<SubscriptionBase> > */
/*         Map; */

/*     for(Map::const_iterator it = subscriptions_.begin(), n = subscriptions_.end(); it != n; ++it) */
/*     { */
/*         if(it->second->type_name() == full_name) */
/*             return dynamic_cast<const ProtoBufMessage&>(it->second->newest()); */
/*     } */

/*     // this shouldn't happen if we properly create our Subscriptions */
/*     throw(std::runtime_error("Invalid message given for call to newest()")); */
/* } */


#endif
