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

#include "goby/common/zeromq_service.h"
#include "goby/util/debug_logger.h"
#include "goby/util/debug_logger/term_color.h"

#include "goby/pb/application.h"

#include "goby/acomms/amac.h"
#include "goby/acomms/queue.h"
#include "goby/acomms/route.h"

#include "goby/acomms/bind.h"
#include "goby/acomms/connect.h"

#include "goby/acomms/protobuf/queue.pb.h"

#include "goby/acomms/protobuf/file_transfer.pb.h"
#include "goby/acomms/protobuf/mm_driver.pb.h"
#include "goby/acomms/protobuf/modem_driver_status.pb.h"
#include "goby/acomms/protobuf/mosh_packet.pb.h"
#include "goby/acomms/protobuf/time_update.pb.h"

#include "bridge_config.pb.h"

using namespace goby::util::logger;

namespace goby
{
namespace acomms
{
class Bridge : public goby::pb::Application, public goby::pb::DynamicProtobufNode
{
  public:
    Bridge(protobuf::BridgeConfig* cfg);
    ~Bridge();

  private:
    void loop();

    void handle_link_ack(const protobuf::ModemTransmission& ack_msg,
                         const google::protobuf::Message& orig_msg, QueueManager* from_queue);

    void handle_queue_receive(const google::protobuf::Message& msg, QueueManager* from_queue);

    void handle_modem_receive(const goby::acomms::protobuf::ModemTransmission& message,
                              QueueManager* in_queue);

    void handle_external_push(std::shared_ptr<google::protobuf::Message> msg,
                              QueueManager* in_queue)
    {
        try
        {
            in_queue->push_message(*msg);
        }
        catch (std::exception& e)
        {
            glog.is(WARN) && glog << "Failed to push message: " << e.what() << std::endl;
        }
    }

    void handle_initiate_transmission(const protobuf::ModemTransmission& m, int subnet);

    void handle_data_request(const protobuf::ModemTransmission& m, int subnet);

    void handle_driver_status(const protobuf::ModemDriverStatus& m, int subnet);

    void generate_hw_ctl_network_ack(QueueManager* in_queue,
                                     goby::acomms::protobuf::NetworkAck::AckType type);
    void generate_time_update_network_ack(QueueManager* in_queue,
                                          goby::acomms::protobuf::NetworkAck::AckType type);

  private:
    protobuf::BridgeConfig& cfg_;

    std::vector<std::shared_ptr<QueueManager> > q_managers_;
    std::vector<std::shared_ptr<MACManager> > mac_managers_;

    RouteManager r_manager_;

    std::shared_ptr<micromodem::protobuf::HardwareControlCommand> pending_hw_ctl_;
    std::shared_ptr<goby::acomms::protobuf::TimeUpdateResponse> pending_time_update_;
    std::uint64_t time_update_request_time_;
};
} // namespace acomms
} // namespace goby

int main(int argc, char* argv[])
{
    goby::acomms::protobuf::BridgeConfig cfg;
    goby::run<goby::acomms::Bridge>(argc, argv, &cfg);
}

using goby::glog;

goby::acomms::Bridge::Bridge(protobuf::BridgeConfig* cfg)
    : Application(cfg), DynamicProtobufNode(&Application::zeromq_service()), cfg_(*cfg),
      time_update_request_time_(0)
{
    glog.is(DEBUG1) && glog << cfg_.DebugString() << std::endl;

    goby::acomms::DCCLCodec::get()->set_cfg(cfg->dccl_cfg());

    // load all shared libraries
    for (int i = 0, n = cfg_.load_shared_library_size(); i < n; ++i)
    {
        glog.is(DEBUG1) && glog << "Loading shared library: " << cfg_.load_shared_library(i)
                                << std::endl;

        void* handle =
            dccl::DynamicProtobufManager::load_from_shared_lib(cfg_.load_shared_library(i));

        if (!handle)
        {
            glog.is(DIE) && glog << "Failed ... check path provided or add to /etc/ld.so.conf "
                                 << "or LD_LIBRARY_PATH" << std::endl;
        }

        glog.is(DEBUG1) && glog << "Loading shared library dccl codecs." << std::endl;

        goby::acomms::DCCLCodec::get()->load_shared_library_codecs(handle);
    }

    // load all .proto files
    dccl::DynamicProtobufManager::enable_compilation();
    for (int i = 0, n = cfg_.load_proto_file_size(); i < n; ++i)
    {
        glog.is(DEBUG1) && glog << "Loading protobuf file: " << cfg_.load_proto_file(i)
                                << std::endl;

        if (!dccl::DynamicProtobufManager::load_from_proto_file(cfg_.load_proto_file(i)))
            glog.is(DIE) && glog << "Failed to load file." << std::endl;
    }

    r_manager_.set_cfg(cfg_.route_cfg());
    q_managers_.resize(cfg_.subnet_size());
    mac_managers_.resize(cfg_.subnet_size());
    for (int i = 0, n = cfg_.subnet_size(); i < n; ++i)
    {
        q_managers_[i].reset(new QueueManager);
        mac_managers_[i].reset(new MACManager);

        goby::acomms::protobuf::QueueManagerConfig qcfg = cfg_.subnet(i).queue_cfg();
        q_managers_[i]->set_cfg(qcfg);

        mac_managers_[i]->startup(cfg_.subnet(i).mac_cfg());

        goby::acomms::bind(*q_managers_[i], r_manager_);

        goby::acomms::connect(
            &(q_managers_[i]->signal_ack),
            boost::bind(&Bridge::handle_link_ack, this, _1, _2, q_managers_[i].get()));

        goby::acomms::connect(
            &(q_managers_[i]->signal_receive),
            boost::bind(&Bridge::handle_queue_receive, this, _1, q_managers_[i].get()));

        Application::subscribe<goby::acomms::protobuf::ModemTransmission>(
            boost::bind(&Bridge::handle_modem_receive, this, _1, q_managers_[i].get()),
            "Rx" + goby::util::as<std::string>(qcfg.modem_id()));

        DynamicProtobufNode::subscribe(
            goby::util::PubSubNodeWrapperBase::SOCKET_SUBSCRIBE,
            boost::bind(&Bridge::handle_external_push, this, _1, q_managers_[i].get()),
            "QueuePush" + goby::util::as<std::string>(qcfg.modem_id()));

        Application::subscribe<goby::acomms::protobuf::ModemTransmission>(
            boost::bind(&Bridge::handle_data_request, this, _1, i),
            "DataRequest" + goby::util::as<std::string>(qcfg.modem_id()));

        Application::subscribe<goby::acomms::protobuf::ModemDriverStatus>(
            boost::bind(&Bridge::handle_driver_status, this, _1, i),
            "Status" + goby::util::as<std::string>(qcfg.modem_id()));

        goby::acomms::connect(&mac_managers_[i]->signal_initiate_transmission,
                              boost::bind(&Bridge::handle_initiate_transmission, this, _1, i));
    }
}

goby::acomms::Bridge::~Bridge() {}

void goby::acomms::Bridge::loop()
{
    for (std::vector<std::shared_ptr<QueueManager> >::iterator it = q_managers_.begin(),
                                                               end = q_managers_.end();
         it != end; ++it)
    { (*it)->do_work(); }

    for (std::vector<std::shared_ptr<MACManager> >::iterator it = mac_managers_.begin(),
         end = mac_managers_.end();
         it != end; ++it)
    { (*it)->do_work(); } std::uint64_t now = goby::util::goby_time<std::uint64_t>();
    if (pending_hw_ctl_ && (pending_hw_ctl_->time() + cfg_.special_command_ttl() * 1000000 < now))
    {
        glog.is(VERBOSE) && glog << "HardwareControlCommand expired." << std::endl;

        generate_hw_ctl_network_ack(q_managers_.at(0).get(),
                                    goby::acomms::protobuf::NetworkAck::EXPIRE);
        pending_hw_ctl_.reset();
    }

    if (pending_time_update_ &&
        (pending_time_update_->time() + cfg_.special_command_ttl() * 1000000 < now))
    {
        glog.is(VERBOSE) && glog << "TimeUpdateRequest expired." << std::endl;

        generate_time_update_network_ack(q_managers_.at(0).get(),
                                         goby::acomms::protobuf::NetworkAck::EXPIRE);
        pending_time_update_.reset();
    }
}

void goby::acomms::Bridge::handle_queue_receive(const google::protobuf::Message& msg,
                                                QueueManager* from_queue)
{
    publish(msg, "QueueRx" + goby::util::as<std::string>(from_queue->modem_id()));

    // handle various command messages
    if (msg.GetDescriptor() == goby::acomms::protobuf::RouteCommand::descriptor())
    {
        goby::acomms::protobuf::RouteCommand route_cmd;
        route_cmd.CopyFrom(msg);
        glog.is(VERBOSE) && glog << "Received RouteCommand: " << msg.DebugString() << std::endl;
        goby::acomms::protobuf::RouteManagerConfig cfg = cfg_.route_cfg();
        cfg.mutable_route()->CopyFrom(route_cmd.new_route());
        r_manager_.set_cfg(cfg);
    }
    else if (msg.GetDescriptor() == micromodem::protobuf::HardwareControlCommand::descriptor())
    {
        pending_hw_ctl_.reset(new micromodem::protobuf::HardwareControlCommand);
        pending_hw_ctl_->CopyFrom(msg);
        if (!pending_hw_ctl_->has_hw_ctl_dest())
            pending_hw_ctl_->set_hw_ctl_dest(pending_hw_ctl_->command_dest());

        glog.is(VERBOSE) && glog << "Received HardwareControlCommand: " << msg.DebugString()
                                 << std::endl;
    }
    else if (msg.GetDescriptor() == goby::acomms::protobuf::TimeUpdateRequest::descriptor())
    {
        goby::acomms::protobuf::TimeUpdateRequest request;
        request.CopyFrom(msg);

        pending_time_update_.reset(new goby::acomms::protobuf::TimeUpdateResponse);
        pending_time_update_->set_time(request.time());
        time_update_request_time_ = request.time();
        pending_time_update_->set_request_src(request.src());
        pending_time_update_->set_src(from_queue->modem_id());
        pending_time_update_->set_dest(request.update_time_for_id());

        glog.is(VERBOSE) && glog << "Received TimeUpdateRequest: " << msg.DebugString()
                                 << std::endl;
    }
}

void goby::acomms::Bridge::handle_link_ack(const protobuf::ModemTransmission& ack_msg,
                                           const google::protobuf::Message& orig_msg,
                                           QueueManager* from_queue)
{
    // publish link ack
    publish(orig_msg, "QueueAckOrig" + goby::util::as<std::string>(from_queue->modem_id()));
}

void goby::acomms::Bridge::handle_modem_receive(
    const goby::acomms::protobuf::ModemTransmission& message, QueueManager* in_queue)
{
    try
    {
        in_queue->handle_modem_receive(message);

        if (cfg_.forward_cacst())
        {
            for (int i = 0, n = message.ExtensionSize(micromodem::protobuf::receive_stat); i < n;
                 ++i)
            {
                micromodem::protobuf::ReceiveStatistics cacst =
                    message.GetExtension(micromodem::protobuf::receive_stat, i);

                glog.is(VERBOSE) && glog << "Forwarding statistics message to topside: " << cacst
                                         << std::endl;
                r_manager_.handle_in(in_queue->meta_from_msg(cacst), cacst, in_queue->modem_id());
            }
        }

        if (cfg_.forward_ranging_reply() &&
            message.HasExtension(micromodem::protobuf::ranging_reply))
        {
            micromodem::protobuf::RangingReply ranging =
                message.GetExtension(micromodem::protobuf::ranging_reply);

            glog.is(VERBOSE) && glog << "Forwarding ranging message to topside: " << ranging
                                     << std::endl;
            r_manager_.handle_in(in_queue->meta_from_msg(ranging), ranging, in_queue->modem_id());
        }

        if (pending_time_update_)
        {
            if (message.type() == goby::acomms::protobuf::ModemTransmission::DRIVER_SPECIFIC &&
                message.GetExtension(micromodem::protobuf::type) ==
                    micromodem::protobuf::MICROMODEM_TWO_WAY_PING)
            {
                micromodem::protobuf::RangingReply range_reply =
                    message.GetExtension(micromodem::protobuf::ranging_reply);

                if (range_reply.one_way_travel_time_size() > 0)
                    pending_time_update_->set_time_of_flight_microsec(
                        range_reply.one_way_travel_time(0) * 1e6);

                glog.is(VERBOSE) && glog << "Received time of flight of "
                                         << pending_time_update_->time_of_flight_microsec()
                                         << " microseconds" << std::endl;
            }
            else if (message.type() == goby::acomms::protobuf::ModemTransmission::ACK &&
                     pending_time_update_->has_time_of_flight_microsec())
            {
                if (message.acked_frame_size() && message.acked_frame(0) == 0)
                {
                    // ack for our response
                    glog.is(VERBOSE) && glog << "Received ack for TimeUpdateResponse" << std::endl;

                    generate_time_update_network_ack(in_queue,
                                                     goby::acomms::protobuf::NetworkAck::ACK);
                    pending_time_update_.reset();
                }
            }
        }

        if (pending_hw_ctl_ &&
            message.type() == goby::acomms::protobuf::ModemTransmission::DRIVER_SPECIFIC &&
            message.GetExtension(micromodem::protobuf::type) ==
                micromodem::protobuf::MICROMODEM_HARDWARE_CONTROL_REPLY)
        {
            micromodem::protobuf::HardwareControl control =
                message.GetExtension(micromodem::protobuf::hw_ctl);

            if (message.src() == pending_hw_ctl_->hw_ctl_dest() &&
                message.dest() == in_queue->modem_id())
            {
                glog.is(VERBOSE) && glog << "Received hardware control response: " << control
                                         << " to our command: " << *pending_hw_ctl_ << std::endl;

                generate_hw_ctl_network_ack(in_queue, goby::acomms::protobuf::NetworkAck::ACK);
                pending_hw_ctl_.reset();
            }
        }
    }
    catch (std::exception& e)
    {
        glog.is(WARN) && glog << "Failed to handle incoming message: " << e.what() << std::endl;
    }
}

void goby::acomms::Bridge::generate_hw_ctl_network_ack(
    QueueManager* in_queue, goby::acomms::protobuf::NetworkAck::AckType type)
{
    protobuf::NetworkAck ack;
    ack.set_ack_src(pending_hw_ctl_->hw_ctl_dest());
    ack.set_message_dccl_id(DCCLCodec::get()->id(pending_hw_ctl_->GetDescriptor()));

    ack.set_message_src(pending_hw_ctl_->command_src());
    ack.set_message_dest(pending_hw_ctl_->command_dest());
    ack.set_message_time(pending_hw_ctl_->time());
    ack.set_ack_type(type);

    r_manager_.handle_in(in_queue->meta_from_msg(ack), ack, in_queue->modem_id());
}

void goby::acomms::Bridge::generate_time_update_network_ack(
    QueueManager* in_queue, goby::acomms::protobuf::NetworkAck::AckType type)
{
    protobuf::NetworkAck ack;
    ack.set_ack_src(pending_time_update_->dest());
    ack.set_message_dccl_id(
        DCCLCodec::get()->id(goby::acomms::protobuf::TimeUpdateRequest::descriptor()));

    ack.set_message_src(pending_time_update_->request_src());
    ack.set_message_dest(pending_time_update_->dest());
    ack.set_message_time(time_update_request_time_);
    ack.set_ack_type(type);

    r_manager_.handle_in(in_queue->meta_from_msg(ack), ack, in_queue->modem_id());
}

void goby::acomms::Bridge::handle_initiate_transmission(const protobuf::ModemTransmission& m,
                                                        int subnet)
{
    // see if we need to override with a time update ping
    if (pending_time_update_ &&
        (m.dest() == pending_time_update_->dest() || m.dest() == QUERY_DESTINATION_ID))
    {
        protobuf::ModemTransmission new_transmission = m;
        if (!pending_time_update_->has_time_of_flight_microsec())
        {
            new_transmission.set_dest(pending_time_update_->dest());
            new_transmission.set_type(goby::acomms::protobuf::ModemTransmission::DRIVER_SPECIFIC);
            new_transmission.SetExtension(micromodem::protobuf::type,
                                          micromodem::protobuf::MICROMODEM_TWO_WAY_PING);
        }
        else
        {
            // send it out!
            new_transmission.set_type(goby::acomms::protobuf::ModemTransmission::DATA);
            new_transmission.set_ack_requested(true);
            new_transmission.set_dest(pending_time_update_->dest());

            pending_time_update_->set_time(goby::util::goby_time<std::uint64_t>());

            goby::acomms::DCCLCodec::get()->encode(new_transmission.add_frame(),
                                                   *pending_time_update_);
        }
        publish(new_transmission,
                "Tx" + goby::util::as<std::string>(cfg_.subnet(subnet).queue_cfg().modem_id()));
    }
    // see if we need to override with a hardware control command
    else if (pending_hw_ctl_ &&
             (m.dest() == pending_hw_ctl_->hw_ctl_dest() || m.dest() == QUERY_DESTINATION_ID))
    {
        protobuf::ModemTransmission new_transmission = m;
        new_transmission.set_dest(pending_hw_ctl_->hw_ctl_dest());
        new_transmission.set_type(goby::acomms::protobuf::ModemTransmission::DRIVER_SPECIFIC);
        new_transmission.SetExtension(micromodem::protobuf::type,
                                      micromodem::protobuf::MICROMODEM_HARDWARE_CONTROL);
        new_transmission.MutableExtension(micromodem::protobuf::hw_ctl)
            ->CopyFrom(pending_hw_ctl_->control());
        publish(new_transmission,
                "Tx" + goby::util::as<std::string>(cfg_.subnet(subnet).queue_cfg().modem_id()));
    }
    else
    {
        publish(m, "Tx" + goby::util::as<std::string>(cfg_.subnet(subnet).queue_cfg().modem_id()));
    }
}

void goby::acomms::Bridge::handle_data_request(const protobuf::ModemTransmission& orig_msg,
                                               int subnet)
{
    protobuf::ModemTransmission msg = orig_msg;
    q_managers_[subnet]->handle_modem_data_request(&msg);
    publish(msg, "DataResponse" +
                     goby::util::as<std::string>(cfg_.subnet(subnet).queue_cfg().modem_id()));
}

void goby::acomms::Bridge::handle_driver_status(const protobuf::ModemDriverStatus& m, int subnet)
{
    glog.is(VERBOSE) && glog << "Forwarding modemdriver status message to topside: "
                             << m.ShortDebugString() << std::endl;
    QueueManager* in_queue = q_managers_[subnet].get();

    r_manager_.handle_in(in_queue->meta_from_msg(m), m, in_queue->modem_id());
}
