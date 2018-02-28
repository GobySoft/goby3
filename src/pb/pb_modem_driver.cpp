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

#include "pb_modem_driver.h"

#include "goby/acomms/modemdriver/driver_exception.h"
#include "goby/acomms/modemdriver/mm_driver.h"
#include "goby/common/logger.h"
#include "goby/util/binary.h"

using goby::util::hex_encode;
using goby::util::hex_decode;
using goby::glog;
using namespace goby::common::logger;
using goby::common::goby_time;


goby::pb::PBDriver::PBDriver(goby::common::ZeroMQService* zeromq_service) :
    StaticProtobufNode(zeromq_service),
    zeromq_service_(zeromq_service),
    last_send_time_(goby_time<uint64>()),
    request_socket_id_(0),
    query_interval_seconds_(1),
    reset_interval_seconds_(120),
    waiting_for_reply_(false),
    next_frame_(0)
{
    on_receipt<acomms::protobuf::StoreServerResponse>(0, &PBDriver::handle_response, this);
}

void goby::pb::PBDriver::startup(const acomms::protobuf::DriverConfig& cfg)
{
    driver_cfg_ = cfg;
    request_.set_modem_id(driver_cfg_.modem_id());
    
    service_cfg_.add_socket()->CopyFrom(
        driver_cfg_.GetExtension(PBDriverConfig::request_socket));
    zeromq_service_->set_cfg(service_cfg_);

    request_socket_id_ =
        driver_cfg_.GetExtension(PBDriverConfig::request_socket).socket_id();

    query_interval_seconds_ =
        driver_cfg_.GetExtension(PBDriverConfig::query_interval_seconds);

    reset_interval_seconds_ =
        driver_cfg_.GetExtension(PBDriverConfig::reset_interval_seconds);

}

void goby::pb::PBDriver::shutdown()
{
    
}

void goby::pb::PBDriver::handle_initiate_transmission(const acomms::protobuf::ModemTransmission& orig_msg)
{
    switch(orig_msg.type())
    {
        case acomms::protobuf::ModemTransmission::DATA:
        {
    
            // buffer the message
            acomms::protobuf::ModemTransmission msg = orig_msg;
            signal_modify_transmission(&msg);
    
            if(driver_cfg_.modem_id() == msg.src())
            {

                if(!msg.has_frame_start())
                    msg.set_frame_start(next_frame_);
    
                // this is our transmission        
                if(msg.rate() < driver_cfg_.ExtensionSize(PBDriverConfig::rate_to_bytes))
                    msg.set_max_frame_bytes(driver_cfg_.GetExtension(PBDriverConfig::rate_to_bytes, msg.rate()));
                else
                    msg.set_max_frame_bytes(driver_cfg_.GetExtension(PBDriverConfig::max_frame_size));

                if(msg.rate() < driver_cfg_.ExtensionSize(PBDriverConfig::rate_to_frames))
                    msg.set_max_num_frames(driver_cfg_.GetExtension(PBDriverConfig::rate_to_frames, msg.rate()));
    
                
                // no data given to us, let's ask for some
                if(msg.frame_size() < (int)msg.max_num_frames())
                    ModemDriverBase::signal_data_request(&msg);

                next_frame_ += msg.frame_size();
                
                // don't send an empty message
                if(msg.frame_size() && msg.frame(0).size())
                {
                    request_.add_outbox()->CopyFrom(msg);
                }
            }
            else
            {
                // send thirdparty "poll"
                msg.SetExtension(PBDriverTransmission::poll_src, msg.src());        
                msg.SetExtension(PBDriverTransmission::poll_dest, msg.dest());

                msg.set_dest(msg.src());
                msg.set_src(driver_cfg_.modem_id());
        
                msg.set_type(goby::acomms::protobuf::ModemTransmission::DRIVER_SPECIFIC);
                msg.SetExtension(PBDriverTransmission::type, PBDriverTransmission::PB_DRIVER_POLL);

                request_.add_outbox()->CopyFrom(msg);
            }

        }    
        break;
        default:
            glog.is(DEBUG1) && glog << group(glog_out_group()) << warn << "Not initiating transmission because we were given an invalid transmission type for the base Driver:" << orig_msg.DebugString() << std::endl;
            break;
                
    }
}


void goby::pb::PBDriver::do_work()
{
    while(zeromq_service_->poll(0))
    { }
    
    // call in with our outbox
    if(!waiting_for_reply_ &&
       request_.IsInitialized() &&
       goby_time<uint64>() > last_send_time_ + 1000000*static_cast<uint64>(query_interval_seconds_))
    {
        static int request_id = 0;
        request_.set_request_id(request_id++);
        glog.is(DEBUG1) && glog << group(glog_out_group()) << "Sending to server." << std::endl;
        glog.is(DEBUG2) && glog << group(glog_out_group()) << "Outbox: " << request_.DebugString() << std::flush;
        send(request_, request_socket_id_);
        last_send_time_ = goby_time<uint64>();
	request_.clear_outbox();
        waiting_for_reply_ = true;
    }
    else if(waiting_for_reply_ &&
	    goby_time<uint64>() > last_send_time_ + 1e6*reset_interval_seconds_)
    {
        glog.is(DEBUG1) && glog << group(glog_out_group()) << warn 
				<< "No response in " << reset_interval_seconds_ << " seconds, resetting socket." << std::endl;
	zeromq_service_->close_all();
        zeromq_service_->set_cfg(service_cfg_);
	waiting_for_reply_ = false;
    }
}

void goby::pb::PBDriver::handle_response(const acomms::protobuf::StoreServerResponse& response)
{
    glog.is(DEBUG1) && glog << group(glog_in_group()) << "Received response in " << (goby_time<uint64_t>() - last_send_time_)/1.0e6 << " seconds." << std::endl;

    glog.is(DEBUG2) && glog << group(glog_in_group()) << "Inbox: " << response.DebugString() << std::flush;

    for(int i = 0, n = response.inbox_size(); i < n; ++i)
    {
        const acomms::protobuf::ModemTransmission& msg = response.inbox(i);

        // poll for us
        if(msg.type() == goby::acomms::protobuf::ModemTransmission::DRIVER_SPECIFIC &&
           msg.GetExtension(PBDriverTransmission::type) == PBDriverTransmission::PB_DRIVER_POLL &&
           msg.GetExtension(PBDriverTransmission::poll_src) == driver_cfg_.modem_id())
        {
            goby::acomms::protobuf::ModemTransmission data_msg = msg;
            data_msg.set_type(goby::acomms::protobuf::ModemTransmission::DATA);
            data_msg.set_src(msg.GetExtension(PBDriverTransmission::poll_src));
            data_msg.set_dest(msg.GetExtension(PBDriverTransmission::poll_dest));

            data_msg.ClearExtension(PBDriverTransmission::type);
            data_msg.ClearExtension(PBDriverTransmission::poll_dest);
            data_msg.ClearExtension(PBDriverTransmission::poll_src);        
            
            handle_initiate_transmission(data_msg);
        }
        else
        {
            // ack any packets
            if(msg.dest() == driver_cfg_.modem_id() &&
               msg.type() == acomms::protobuf::ModemTransmission::DATA &&
               msg.ack_requested())
            {
                acomms::protobuf::ModemTransmission& ack = *request_.add_outbox();
                ack.set_type(goby::acomms::protobuf::ModemTransmission::ACK);
                ack.set_src(msg.dest());
                ack.set_dest(msg.src());
                for(int i = msg.frame_start(), n = msg.frame_size() + msg.frame_start(); i < n; ++i)
                    ack.add_acked_frame(i);
            
            }
        
            signal_receive(msg);
        }   
    }

    waiting_for_reply_ = false;
}
