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

#ifndef PROTOBUFPUBSUBNODE20110506H
#define PROTOBUFPUBSUBNODE20110506H

#include "goby/common/pubsub_node_wrapper.h"
#include "protobuf_node.h"
#include <boost/function.hpp>

namespace goby
{
namespace pb
{
class StaticProtobufPubSubNodeWrapper : public common::PubSubNodeWrapper<google::protobuf::Message>
{
  public:
    StaticProtobufPubSubNodeWrapper(StaticProtobufNode* node,
                                    const common::protobuf::PubSubSocketConfig& cfg)
        : common::PubSubNodeWrapper<google::protobuf::Message>(node, cfg), node_(*node)
    {
    }

    ~StaticProtobufPubSubNodeWrapper() {}

    template <typename ProtoBufMessage>
    void subscribe(boost::function<void(const ProtoBufMessage&)> handler,
                   const std::string& group = "")
    {
        if (!using_pubsub())
        {
            glog.is(goby::common::logger::WARN) &&
                glog << "Ignoring subscribe since we have `using_pubsub`=false" << std::endl;
            return;
        }
        node_.subscribe<ProtoBufMessage>(SOCKET_SUBSCRIBE, handler, group);
    }

    /* template<typename ProtoBufMessage> */
    /*     const ProtoBufMessage& newest() const */
    /* { */
    /*     return node_.newest<ProtoBufMessage>(); */
    /* } */

  private:
    StaticProtobufNode& node_;
};

class DynamicProtobufPubSubNodeWrapper : public common::PubSubNodeWrapper<google::protobuf::Message>
{
  public:
    DynamicProtobufPubSubNodeWrapper(DynamicProtobufNode* node,
                                     const common::protobuf::PubSubSocketConfig& cfg)
        : common::PubSubNodeWrapper<google::protobuf::Message>(node, cfg), node_(*node)
    {
    }

    ~DynamicProtobufPubSubNodeWrapper() {}

  private:
    DynamicProtobufNode& node_;
};

} // namespace pb
} // namespace goby

#endif
