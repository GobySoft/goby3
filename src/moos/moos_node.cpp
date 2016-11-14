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

#include "moos_node.h"

#include "goby/common/logger.h"

#include "goby/util/binary.h"

using goby::common::ZeroMQService;
using goby::glog;
using namespace goby::common::logger_lock;
using namespace goby::common::logger;


goby::moos::MOOSNode::MOOSNode(ZeroMQService* service)
    : goby::common::NodeInterface<CMOOSMsg>(service)
{
    glog.add_group("in_hex", common::Colors::green, "Goby MOOS (hex) - Incoming");
    glog.add_group("out_hex", common::Colors::magenta, "Goby MOOS (hex) - Outgoing");

}


void goby::moos::MOOSNode::inbox(int marshalling_scheme,
                                 const std::string& identifier,
                                 const std::string& bytes,
                                 int socket_id)
{

    glog.is(DEBUG2) && 
        glog << group("in_hex") << "Received marshalling scheme: " << marshalling_scheme << std::endl;
    
    if(marshalling_scheme == goby::common::MARSHALLING_MOOS)
    {
        boost::shared_ptr<CMOOSMsg> msg(new CMOOSMsg);

        glog.is(DEBUG2) && 
            glog << group("in_hex") << goby::util::hex_encode(bytes) << std::endl;
        MOOSSerializer::parse(msg.get(), bytes);

        const std::string& key = msg->GetKey();
        newest_vars[key] = msg;
        moos_inbox(*msg);
    }
}

void goby::moos::MOOSNode::send(const CMOOSMsg& msg, int socket_id, const std::string& group_unused)
{            

    std::string bytes;
    MOOSSerializer::serialize(msg, &bytes);

    glog.is(DEBUG1) &&
        glog << "Sent: " << "CMOOSMsg/" << msg.GetKey() << "/"  << std::endl;


    glog.is(DEBUG2) &&
        glog << group("out_hex") << goby::util::hex_encode(bytes) << std::endl;

    zeromq_service()->send(goby::common::MARSHALLING_MOOS, "CMOOSMsg/" + msg.GetKey() + "/", bytes, socket_id);
}

void goby::moos::MOOSNode::subscribe(const std::string& full_or_partial_moos_name, int socket_id)
{
    std::string trimmed_name = boost::trim_copy(full_or_partial_moos_name);
    unsigned size = trimmed_name.size();
    if(!size)
    {
        zeromq_service()->subscribe(goby::common::MARSHALLING_MOOS, "CMOOSMsg/", socket_id);
    }
    else if(trimmed_name[size-1] == '*')
    {
        zeromq_service()->subscribe(goby::common::MARSHALLING_MOOS, "CMOOSMsg/" + trimmed_name.substr(0, size-1), socket_id);
    }
    else
    {
        zeromq_service()->subscribe(goby::common::MARSHALLING_MOOS, "CMOOSMsg/" + trimmed_name + "/", socket_id);
    }
}

void goby::moos::MOOSNode::unsubscribe(const std::string& full_or_partial_moos_name, int socket_id)
{
    std::string trimmed_name = boost::trim_copy(full_or_partial_moos_name);
    unsigned size = trimmed_name.size();
    if(!size)
    {
        glog.is(WARN) && glog << warn << "Not unsubscribing for empty string!" << std::endl;
    }
    else if(trimmed_name[size-1] == '*')
    {
        zeromq_service()->unsubscribe(goby::common::MARSHALLING_MOOS, "CMOOSMsg/" + trimmed_name.substr(0, size-1), socket_id);
    }
    else
    {
        zeromq_service()->unsubscribe(goby::common::MARSHALLING_MOOS, "CMOOSMsg/" + trimmed_name + "/", socket_id);
    }
}




CMOOSMsg& goby::moos::MOOSNode::newest(const std::string& key)
{
    if(!newest_vars.count(key))
        newest_vars[key] = boost::shared_ptr<CMOOSMsg>(new CMOOSMsg);
    
    return *newest_vars[key];
}

std::vector<CMOOSMsg> goby::moos::MOOSNode::newest_substr(const std::string& substring)
{    
    std::vector<CMOOSMsg> out;

    std::string trimmed_substring = boost::trim_copy(substring);
    unsigned size = trimmed_substring.size();

    if(size)
    {
        if(trimmed_substring[size-1] == '*')
        {
            trimmed_substring = trimmed_substring.substr(0, size-1);
        }
        else 
        {
            if(newest_vars.count(trimmed_substring))
                out.push_back(*newest_vars[trimmed_substring]);
            return out;
        }
    }
    
    for(std::map<std::string, boost::shared_ptr<CMOOSMsg> >::const_iterator it = newest_vars.begin(), end = newest_vars.end(); it != end; ++it)
    {

        if(!size || it->first.compare(0, trimmed_substring.size(), trimmed_substring) == 0)
            out.push_back(*(it->second));
    }
    return out;
}


