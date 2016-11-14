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

#include "protobuf_node.h"

#include "goby/common/logger.h"

using goby::glog;
using goby::util::as;
using namespace goby::common::logger;

void goby::pb::ProtobufNode::inbox(int marshalling_scheme,
                                   const std::string& identifier,
                                   const std::string& body,
                                   int socket_id)
{
    if(marshalling_scheme == common::MARSHALLING_PROTOBUF)
    {
        std::string::size_type first_slash = identifier.find("/");
        std::string group = identifier.substr(0, first_slash);
        std::string pb_full_name = identifier.substr(first_slash + 1);
        pb_full_name.erase(pb_full_name.size()-1); // final slash
        
        glog.is(DEBUG3) && glog << "MARSHALLING_PROTOBUF type: [" << pb_full_name << "], group: [" << group << "]" << std::endl;

        protobuf_inbox(pb_full_name, body, socket_id, group);
    }
    
}


void goby::pb::ProtobufNode::send(const google::protobuf::Message& msg, int socket_id, const std::string& group)
{
    if(!msg.IsInitialized())
    {
        glog.is(DEBUG1) && glog << warn << "Cannot send message of type [" << msg.GetDescriptor()->full_name() << "] because not all required fields are set." << std::endl;
        return;    
    }
    
    std::string body;
    msg.SerializeToString(&body);

    std::string identifier =  group + "/" + msg.GetDescriptor()->full_name() + "/";
    
    zeromq_service()->send(common::MARSHALLING_PROTOBUF, identifier, body, socket_id);
}
            
void goby::pb::ProtobufNode::subscribe(const std::string& protobuf_type_name, int socket_id, const std::string& group)
{
    subscribe(group + "/" + protobuf_type_name + (protobuf_type_name.empty() ? "" : "/"), socket_id);
}


void goby::pb::ProtobufNode::subscribe(const std::string& identifier, int socket_id)
{
    glog.is(DEBUG1) && glog << "Subscribing for MARSHALLING_PROTOBUF type: " << identifier << std::endl;
    zeromq_service()->subscribe(common::MARSHALLING_PROTOBUF, identifier, socket_id);
}

void goby::pb::StaticProtobufNode::protobuf_inbox(const std::string& protobuf_type_name,
                                                  const std::string& body,
                                                  int socket_id,
                                                  const std::string& group
    )
{
    typedef boost::unordered_multimap<std::string, boost::shared_ptr<SubscriptionBase> >::iterator It;

    std::pair<It,It> it_range = subscriptions_.equal_range(protobuf_type_name);

    for(It it = it_range.first; it != it_range.second; ++it)
    {
        const std::string& current_group = it->second->group();
        if(current_group.empty() || current_group == group)
            it->second->post(body);
    }
}




void goby::pb::DynamicProtobufNode::protobuf_inbox(const std::string& protobuf_type_name,
                                                   const std::string& body,
                                                   int socket_id,
                                                   const std::string& group)
{
    try
    {
        
        boost::shared_ptr<google::protobuf::Message> msg =
            goby::util::DynamicProtobufManager::new_protobuf_message(protobuf_type_name);
        msg->ParseFromString(body);
        
        boost::unordered_multimap<std::string, boost::function<void (boost::shared_ptr<google::protobuf::Message> msg)> >::iterator it =
            subscriptions_.find(group);
        
        if(it != subscriptions_.end())
            it->second(msg);
    }
    catch(std::exception& e)
    {
        glog.is(WARN) && glog << e.what() << std::endl;
    }    
}

void goby::pb::DynamicProtobufNode::on_receipt(
    int socket_id,
    boost::function<void (boost::shared_ptr<google::protobuf::Message> msg)> handler,
    const std::string& group)
{    
    using goby::glog;
    glog.is(goby::common::logger::DEBUG1) && 
        glog << "registering on_receipt handler for group: " << group  << std::endl;

    subscriptions_.insert(std::make_pair(group, handler));
}

void goby::pb::DynamicProtobufNode::subscribe(
    int socket_id,
    boost::function<void (boost::shared_ptr<google::protobuf::Message> msg)> handler,
    const std::string& group)
{
    on_receipt(socket_id, handler, group);
    ProtobufNode::subscribe("", socket_id, group);
}
