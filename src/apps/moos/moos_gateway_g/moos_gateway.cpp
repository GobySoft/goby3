// Copyright 2009-2018 Toby Schneider (http://gobysoft.org/index.wt/people/toby)
//                     GobySoft, LLC (2013-)
//                     Massachusetts Institute of Technology (2007-2014)
//
//
// This file is part of the Goby Underwater Autonomy Project Binaries
// ("The Goby Binaries").
//
// The Goby Binaries are free software: you can redistribute them and/or modify
// them under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 2 of the License, or
// (at your option) any later version.
//
// The Goby Binaries are distributed in the hope that they will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with Goby.  If not, see <http://www.gnu.org/licenses/>.

#include "goby/moos/moos_node.h"
#include "goby/common/logger.h"
#include "goby/common/zeromq_application_base.h"
#include "goby/common/pubsub_node_wrapper.h"
#include "goby/common/logger/term_color.h"
#include "goby/pb/protobuf_node.h"
#include "goby/pb/protobuf_pubsub_node_wrapper.h"
#include "goby/moos/moos_protobuf_helpers.h"

#include "moos_gateway_config.pb.h"

using namespace goby::common::logger;


bool MOOSGateway_OnConnect(void* pParam);
bool MOOSGateway_OnDisconnect(void* pParam);

namespace goby
{
    namespace moos
    {
        class MOOSGateway : public goby::common::ZeroMQApplicationBase, public MOOSNode, public goby::pb::DynamicProtobufNode
        {
        public:
            MOOSGateway(protobuf::MOOSGatewayConfig* cfg);
            ~MOOSGateway();

            friend bool ::MOOSGateway_OnConnect(void* pParam);
            friend bool ::MOOSGateway_OnDisconnect(void* pParam);
            
        private:
            void moos_inbox(CMOOSMsg& msg);
            void loop();
            
            void check_for_new_moos_variables();
            bool clears_subscribe_filters(const std::string& moos_variable);

            void pb_inbox(boost::shared_ptr<google::protobuf::Message> msg, const std::string& group);
            
        private:
            static goby::common::ZeroMQService zeromq_service_;
            protobuf::MOOSGatewayConfig& cfg_;

            goby::common::PubSubNodeWrapper<CMOOSMsg> goby_moos_pubsub_client_;
            CMOOSCommClient moos_client_;
            goby::pb::DynamicProtobufPubSubNodeWrapper goby_pb_pubsub_client_;

            enum { MAX_CONNECTION_TIMEOUT = 10 };
            

            std::set<std::string> subscribed_vars_;

            // MOOS Var -> PB Group
            std::multimap<std::string, std::string> moos2pb_;

            // PB Group -> MOOS Var
            std::multimap<std::string, std::string> pb2moos_;

            bool moos_resubscribe_required_;

        };
    }
}

goby::common::ZeroMQService goby::moos::MOOSGateway::zeromq_service_;

int main(int argc, char* argv[])
{
    goby::moos::protobuf::MOOSGatewayConfig cfg;
    goby::run<goby::moos::MOOSGateway>(argc, argv, &cfg);
}

using goby::glog;

goby::moos::MOOSGateway::MOOSGateway(protobuf::MOOSGatewayConfig* cfg)
    : ZeroMQApplicationBase(&zeromq_service_, cfg),
      MOOSNode(&zeromq_service_),
      goby::pb::DynamicProtobufNode(&zeromq_service_),
      cfg_(*cfg),
      goby_moos_pubsub_client_(this, cfg_.pb_convert() ? goby::common::protobuf::PubSubSocketConfig() : cfg_.base().pubsub_config()),
      goby_pb_pubsub_client_(this, cfg_.pb_convert() ? cfg_.base().pubsub_config() : goby::common::protobuf::PubSubSocketConfig()),
      moos_resubscribe_required_(false)
{

    goby::util::DynamicProtobufManager::enable_compilation();

    // load all shared libraries
    for(int i = 0, n = cfg_.load_shared_library_size(); i < n; ++i)
    {
        glog.is(VERBOSE) &&
            glog << "Loading shared library: " << cfg_.load_shared_library(i) << std::endl;
        
        void* handle = dlopen(cfg_.load_shared_library(i).c_str(), RTLD_LAZY);
        if(!handle)
        {
            glog << die << "Failed ... check path provided or add to /etc/ld.so.conf "
                 << "or LD_LIBRARY_PATH" << std::endl;
        }
    }
    
    
    // load all .proto files
    for(int i = 0, n = cfg_.load_proto_file_size(); i < n; ++i)
    {
        glog.is(VERBOSE) &&
            glog << "Loading protobuf file: " << cfg_.load_proto_file(i) << std::endl;

        
        if(!goby::util::DynamicProtobufManager::find_descriptor(
               cfg_.load_proto_file(i)))
            glog.is(DIE) && glog << "Failed to load file." << std::endl;
    }


    
    moos_client_.SetOnConnectCallBack(MOOSGateway_OnConnect,this);
    moos_client_.SetOnDisconnectCallBack(MOOSGateway_OnDisconnect,this);
    moos_client_.Run(cfg_.moos_server_host().c_str(), cfg_.moos_server_port(), cfg_.base().app_name().c_str(), cfg_.moos_comm_tick());

    glog.is(VERBOSE) &&
        glog << "Waiting to connect to MOOSDB ... " << std::endl;

    int i = 0;
    while(!moos_client_.IsConnected())
    {
        i++;
        if(i > MAX_CONNECTION_TIMEOUT)
        {
            glog.is(DIE) &&
                glog << "Failed to connect to MOOSDB in " << MAX_CONNECTION_TIMEOUT << " seconds. Check `moos_server_host` and `moos_server_port`" << std::endl;
        }
        
        sleep(1);
    }

    for(int i = 0, n = cfg_.goby_subscribe_filter_size(); i < n; ++i)
    {
      if(!cfg_.pb_convert())
        goby_moos_pubsub_client_.subscribe(cfg_.goby_subscribe_filter(i));
      else
        goby_pb_pubsub_client_.subscribe(cfg_.goby_subscribe_filter(i));
    }
    

    for(int i = 0, n = cfg_.pb_pair_size(); i < n; ++i)
    {
        if(cfg_.pb_pair(i).direction() == protobuf::MOOSGatewayConfig::ProtobufMOOSBridgePair::PB_TO_MOOS)
        {
            pb2moos_.insert(std::make_pair(cfg_.pb_pair(i).pb_group(), cfg_.pb_pair(i).moos_var()));
            goby::pb::DynamicProtobufNode::subscribe(goby::common::PubSubNodeWrapperBase::SOCKET_SUBSCRIBE,
                                                     boost::bind(&MOOSGateway::pb_inbox, this, _1, cfg_.pb_pair(i).pb_group()),
                                                     cfg_.pb_pair(i).pb_group());
        }
        else if(cfg_.pb_pair(i).direction() == protobuf::MOOSGatewayConfig::ProtobufMOOSBridgePair::MOOS_TO_PB)
        {
            moos2pb_.insert(std::make_pair(cfg_.pb_pair(i).moos_var(), cfg_.pb_pair(i).pb_group()));
            if(!subscribed_vars_.count(cfg_.pb_pair(i).moos_var()))
            {
                moos_client_.Register(cfg_.pb_pair(i).moos_var(), 0);
                subscribed_vars_.insert(cfg_.pb_pair(i).moos_var());
            }
        }
        
    }    

    
    glog.add_group("from_moos", common::Colors::lt_magenta, "MOOS -> Goby");
    glog.add_group("to_moos", common::Colors::lt_green, "Goby -> MOOS");
}


goby::moos::MOOSGateway::~MOOSGateway()
{
}


void goby::moos::MOOSGateway::moos_inbox(CMOOSMsg& msg)
{
    // we wrote this, so ignore
    if(msg.GetSourceAux().find(application_name()) != std::string::npos)
        return;
    
    // identify us as the writer
    msg.SetSourceAux(msg.GetSourceAux() +
                     (msg.GetSourceAux().size() ? "/" : "")
                     + application_name());
    
    glog.is(VERBOSE) &&
        glog << group("to_moos") << msg << std::endl;

    moos_client_.Post(msg);

}

    

// from MinimalApplicationBase
void goby::moos::MOOSGateway::loop()
{
    check_for_new_moos_variables();
    std::list<CMOOSMsg> moos_msgs;
    
    moos_client_.Fetch(moos_msgs);

    BOOST_FOREACH(CMOOSMsg& msg, moos_msgs)
    {
        // we wrote this, so ignore
        if(msg.GetSourceAux().find(application_name()) != std::string::npos)
            continue;

        msg.SetSourceAux(msg.GetSourceAux() +
                         (msg.GetSourceAux().size() ? "/" : "")
                         + application_name());

        glog.is(VERBOSE) &&
            glog << group("from_moos") << msg << std::endl;    
        
	if(!cfg_.pb_convert())
	  goby_moos_pubsub_client_.publish(msg);

        
        
        if(cfg_.pb_convert() && moos2pb_.count(msg.GetKey()))
        {
            boost::shared_ptr<google::protobuf::Message> pbmsg =
                dynamic_parse_for_moos(msg.GetString());
            if(pbmsg)
            {                
                typedef std::multimap<std::string, std::string>::iterator It;
                std::pair<It, It> it_range = moos2pb_.equal_range(msg.GetKey());
                for(It it = it_range.first; it != it_range.second; ++it)
                    goby_pb_pubsub_client_.publish(*pbmsg, it->second);
            }
            
        }
    }

}

// adapted from CMOOSLogger::HandleWildCardLogging
// as MOOS has no "accepted" way of handling wild card subscriptions
void goby::moos::MOOSGateway::check_for_new_moos_variables()
{
    const double DEFAULT_WILDCARD_TIME = 1.0;
    static double dfLastWildCardTime = -1.0;
    if(MOOSTime()-dfLastWildCardTime > DEFAULT_WILDCARD_TIME)
    {
        MOOSMSG_LIST InMail;
        if(moos_client_.ServerRequest("VAR_SUMMARY", InMail, 2.0, false))
        {
            if(InMail.size()!=1)
            {
                glog.is(WARN) &&
                    glog << "ServerRequest for VAR_SUMMARY returned incorrect mail size (should be one)";
                return;
            }
            
            std::vector<std::string> all_var;
            std::string var_str(InMail.begin()->GetString());
            boost::split(all_var, var_str, boost::is_any_of(","));

            BOOST_FOREACH(const std::string& s, all_var)
            {
                if(!subscribed_vars_.count(s))
                {
                    if(clears_subscribe_filters(s))
                    {
                        glog.is(VERBOSE) &&
                            glog << "moos_client_.Register for " << s << std::endl;
                        moos_client_.Register(s, 0);
                        subscribed_vars_.insert(s);
                    }
                }
            }
            
            dfLastWildCardTime = MOOSTime();
        }
    }
}

bool goby::moos::MOOSGateway::clears_subscribe_filters(const std::string& moos_variable)
{
    for(int i = 0, n = cfg_.moos_subscribe_filter_size(); i < n; ++i)
    {
        if(moos_variable.compare(0, cfg_.moos_subscribe_filter(i).size(), cfg_.moos_subscribe_filter(i)) == 0)
            return true;
    }
    return false;    
}
    

void goby::moos::MOOSGateway::pb_inbox(boost::shared_ptr<google::protobuf::Message> msg, const std::string& group)
{
    glog.is(DEBUG2) && glog << "PB --> MOOS: Group: " << group << ", msg type: " << msg->GetDescriptor()->full_name() << std::endl;

    std::string serialized;
    serialize_for_moos(&serialized,*msg);

    typedef std::multimap<std::string, std::string>::iterator It;
    std::pair<It, It> it_range = pb2moos_.equal_range(group);

    for(It it = it_range.first; it != it_range.second; ++it)
        moos_client_.Notify(it->second, serialized);
}

bool MOOSGateway_OnConnect(void* pParam)
{
    if(pParam)
    {
        goby::moos::MOOSGateway* pApp = (goby::moos::MOOSGateway*)pParam;

        if(pApp->moos_resubscribe_required_)
        {
            for(std::set<std::string>::const_iterator it = pApp->subscribed_vars_.begin(), end = pApp->subscribed_vars_.end(); it != end; ++it)
            {
                pApp->moos_client_.Register(*it, 0);
                glog.is(DEBUG2) && glog << "Resubscribing for: " << *it << std::endl;
            }
        }
        
        return true;
    }
    else
    {
        return false;
    }
}

bool MOOSGateway_OnDisconnect(void* pParam)
{
    if(pParam)
    {
        goby::moos::MOOSGateway* pApp = (goby::moos::MOOSGateway*)pParam;
        pApp->moos_resubscribe_required_ = true;
        
        return true;
    }
    else
    {
        return false;
    }
}
