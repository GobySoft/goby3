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

#ifndef APPLICATION20100908H
#define APPLICATION20100908H

#include "goby/common/logger.h"
#include "goby/common/core_helpers.h"
#include "goby/common/exception.h"

#include "goby/pb/protobuf/config.pb.h"
#include "goby/pb/protobuf/header.pb.h"

#include "protobuf_pubsub_node_wrapper.h"
#include "goby/common/zeromq_application_base.h"


namespace google { namespace protobuf { class Message; } }
namespace goby
{
        
    /// Contains objects relating to the core publish / subscribe architecture provided by Goby.
    namespace pb
    {
        /// Base class provided for users to generate applications that participate in the Goby publish/subscribe architecture.
        class Application : public common::ZeroMQApplicationBase
        {
          protected:
            // make these more accessible
            typedef goby::common::Colors Colors;
            
            /// \name Constructors / Destructor
            //@{
            /// \param cfg pointer to object derived from google::protobuf::Message that defines the configuration for this Application. This constructor will use the Description of `cfg` to read the command line parameters and configuration file (if given) and use these values to populate `cfg`. `cfg` must be a static member of the subclass or global object since member objects will be constructed *after* the Application constructor is called.
            Application(google::protobuf::Message* cfg = 0);
            virtual ~Application();
            //@}            


            /// \name Publish / Subscribe
            //@{            

            void publish(const google::protobuf::Message& msg, const std::string& group = "");

            //@}

            
            /// \brief Subscribe to a message (of any type derived from google::protobuf::Message)            
            ///
            /// \param handler Function object to be called as soon as possible upon receipt of a message of this type. The signature of `handler` must match: void handler(const ProtoBufMessage& msg). if `handler` is omitted, no handler is called and only the newest message buffer is updated upon message receipt (for calls to newest<ProtoBufMessage>())
             template<typename ProtoBufMessage>
                void subscribe(
                    boost::function<void (const ProtoBufMessage&)> handler =
                    boost::function<void (const ProtoBufMessage&)>(),
                    const std::string& group = ""
                    )
             {
                 if(pubsub_node_)
                     pubsub_node_->subscribe<ProtoBufMessage>(handler, group);
             }
             
            
            /// \brief Subscribe for a type using a class member function as the handler
            /// 
            /// \param mem_func Member function (method) of class C with a signature of void C::mem_func(const ProtoBufMessage& msg)
            /// \param obj pointer to the object whose member function (mem_func) to call
            template<typename ProtoBufMessage, class C>
                void subscribe(void(C::*mem_func)(const ProtoBufMessage&),
                               C* obj,
                               const std::string& group = "")
            {
                if(pubsub_node_)
                    pubsub_node_->subscribe<ProtoBufMessage>(boost::bind(mem_func, obj, _1), group);
            }
            
            /// \name Message Accessors
            //@{
            /// \brief Fetchs the newest received message of this type 
            ///
            /// You must subscribe() for this type before using this method
/*             template<typename ProtoBufMessage> */
/*                 const ProtoBufMessage& newest() */
/*             { */
/*                 if(pubsub_node_) */
/*                     return pubsub_node_->newest<ProtoBufMessage>(); */
/*                 else */
/*                     throw(goby::Exception("not using pubsub, can't call newest")); */
/* 	    } */
            
            //@}            

            common::ZeroMQService& zeromq_service()
            {
                return zeromq_service_;
            }


            boost::shared_ptr<StaticProtobufPubSubNodeWrapper> pubsub_node()
            {
                return pubsub_node_;
            }
            
          private:
            Application(const Application&);
            Application& operator= (const Application&);

            void __set_up_sockets();            

            
          private:
            common::ZeroMQService zeromq_service_;
            boost::shared_ptr<StaticProtobufNode> protobuf_node_;
            boost::shared_ptr<StaticProtobufPubSubNodeWrapper> pubsub_node_;
        };
    }
}

#endif
