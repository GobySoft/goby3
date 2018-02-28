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

#ifndef SUBSCRIPTION20110412H
#define SUBSCRIPTION20110412H

#include <boost/function.hpp>
#include <boost/shared_ptr.hpp>
#include <google/protobuf/descriptor.h>
#include <google/protobuf/dynamic_message.h>



namespace goby
{
    namespace pb
    {
        // forms a non-template base for the Subscription class, allowing us
        // use a common pointer type.
        class SubscriptionBase
        {
          public:
            virtual void post(const std::string& body) = 0;
            virtual const google::protobuf::Message& newest() const = 0;
            virtual const std::string& type_name() const = 0;
            virtual const std::string& group() const = 0;
            virtual bool has_valid_handler() const = 0;
        };

        // forms the concept of a subscription to a given Google Protocol Buffers
        // type ProtoBufMessage
        // An instantiation of this is created for each call to ApplicationBase::subscribe()
        template<typename ProtoBufMessage>
            class Subscription : public SubscriptionBase
        {
          public:
            typedef boost::function<void (const ProtoBufMessage&)> HandlerType;

          Subscription(HandlerType& handler,
                       const std::string& type_name,
                       const std::string& group = "")
              : handler_(handler),
                type_name_(type_name),
                group_(group)
                { }
            
            // handle an incoming message (serialized using the google::protobuf
            // library calls)
            void post(const std::string& body)
            {
                newest_msg_.ParseFromString(body);
                if(handler_) handler_(newest_msg_);

            }

            // getters
            const google::protobuf::Message& newest() const { return newest_msg_; }
            const std::string& type_name() const { return type_name_; }
            const std::string& group() const { return group_; }
            bool has_valid_handler() const { return handler_; }
            
            
          private:
            HandlerType handler_;
            ProtoBufMessage newest_msg_;
            const std::string type_name_;
            const std::string group_;
        };
    }
}



#endif
