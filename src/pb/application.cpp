// Copyright 2009-2017 Toby Schneider (http://gobysoft.org/index.wt/people/toby)
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

#include <iostream>

#include "goby/common/time.h"

#include "application.h"

using namespace goby::pb;
using namespace goby::common;
using namespace goby::util;
using namespace goby::common::logger;
using boost::shared_ptr;
using goby::glog;
using goby::common::operator<<;


goby::pb::Application::Application(google::protobuf::Message* cfg /*= 0*/)
    : ZeroMQApplicationBase(&zeromq_service_, cfg)
{
    
    __set_up_sockets();
    
    // notify others of our configuration for logging purposes
    if(cfg) publish(*cfg);
}

goby::pb::Application::~Application()
{
    glog.is(DEBUG1) &&
        glog << "Application destructing..." << std::endl;    
}



void goby::pb::Application::__set_up_sockets()
{

    if(!(base_cfg().pubsub_config().has_publish_socket() && base_cfg().pubsub_config().has_subscribe_socket()))
    {glog.is(WARN) &&
            glog << "Not using publish subscribe config. You will need to set up your nodes manually" << std::endl;
    }
    else
    {
        protobuf_node_.reset(new StaticProtobufNode(&zeromq_service_));
        pubsub_node_.reset(new StaticProtobufPubSubNodeWrapper(protobuf_node_.get(), base_cfg().pubsub_config()));
    }
    
    zeromq_service_.merge_cfg(base_cfg().additional_socket_config());
}

void goby::pb::Application::publish(const google::protobuf::Message& msg, const std::string& group)
{
    glog.is(DEBUG3) &&
        glog << "< [" << group << "]: " << msg << std::endl;

    if(pubsub_node_)
        pubsub_node_->publish(msg, group);
}
