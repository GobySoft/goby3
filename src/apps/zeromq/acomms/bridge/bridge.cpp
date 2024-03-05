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

#include "goby/middleware/marshalling/protobuf.h"

#include "goby/acomms/amac.h"
#include "goby/acomms/bind.h"
#include "goby/acomms/connect.h"
#include "goby/acomms/protobuf/file_transfer.pb.h"
#include "goby/acomms/protobuf/mm_driver.pb.h"
#include "goby/acomms/protobuf/modem_driver_status.pb.h"
#include "goby/acomms/protobuf/mosh_packet.pb.h"
#include "goby/acomms/protobuf/queue.pb.h"
#include "goby/acomms/protobuf/time_update.pb.h"
#include "goby/acomms/queue.h"
#include "goby/acomms/route.h"
#include "goby/middleware/acomms/groups.h"
#include "goby/zeromq/application/single_thread.h"
#include "goby/zeromq/protobuf/bridge_config.pb.h"

using goby::glog;
using namespace goby::util::logger;
using goby::acomms::protobuf::ModemTransmission;

namespace goby
{
namespace apps
{
namespace zeromq
{
namespace acomms
{
class Bridge : public goby::zeromq::SingleThreadApplication<protobuf::BridgeConfig>
{
  public:
    Bridge();
    ~Bridge();

  private:
    void loop();

    void handle_link_ack(const goby::acomms::protobuf::ModemTransmission& ack_msg,
                         const google::protobuf::Message& orig_msg,
                         goby::acomms::QueueManager* from_queue);

    void handle_queue_receive(const google::protobuf::Message& msg,
                              goby::acomms::QueueManager* from_queue);

    void handle_modem_receive(const goby::acomms::protobuf::ModemTransmission& message,
                              goby::acomms::QueueManager* in_queue);

    void handle_external_push(std::shared_ptr<const google::protobuf::Message> msg,
                              goby::acomms::QueueManager* in_queue)
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

    void handle_initiate_transmission(const goby::acomms::protobuf::ModemTransmission& m,
                                      int subnet);

    void handle_data_request(const goby::acomms::protobuf::ModemTransmission& m, int subnet);

    void handle_driver_status(const goby::acomms::protobuf::ModemDriverStatus& m, int subnet);

    void generate_hw_ctl_network_ack(goby::acomms::QueueManager* in_queue,
                                     goby::acomms::protobuf::NetworkAck::AckType type);
    void generate_time_update_network_ack(goby::acomms::QueueManager* in_queue,
                                          goby::acomms::protobuf::NetworkAck::AckType type);

  private:
    std::vector<boost::shared_ptr<goby::acomms::QueueManager>> q_managers_;
    std::vector<boost::shared_ptr<goby::acomms::MACManager>> mac_managers_;

    goby::acomms::RouteManager r_manager_;

    boost::shared_ptr<goby::acomms::micromodem::protobuf::HardwareControlCommand> pending_hw_ctl_;
    boost::shared_ptr<goby::acomms::protobuf::TimeUpdateResponse> pending_time_update_;
    std::uint64_t time_update_request_time_;

    struct SubscribeGroups
    {
        SubscribeGroups(int modem_id)
            : rx(goby::middleware::acomms::groups::rx, modem_id),
              queue_push(goby::middleware::acomms::groups::queue_push, modem_id),
              data_request(goby::middleware::acomms::groups::data_request, modem_id),
              status(goby::middleware::acomms::groups::status, modem_id)
        {
        }

        goby::middleware::DynamicGroup rx;
        goby::middleware::DynamicGroup queue_push;
        goby::middleware::DynamicGroup data_request;
        goby::middleware::DynamicGroup status;
    };

    std::map<int, SubscribeGroups> subscribe_groups_;
};
} // namespace acomms
} // namespace zeromq
} // namespace apps
} // namespace goby

int main(int argc, char* argv[]) { goby::run<goby::apps::zeromq::acomms::Bridge>(argc, argv); }

goby::apps::zeromq::acomms::Bridge::Bridge()
    : goby::zeromq::SingleThreadApplication<protobuf::BridgeConfig>(10 * boost::units::si::hertz),
      time_update_request_time_(0)
{
    goby::acomms::DCCLCodec::get()->set_cfg(cfg().dccl_cfg());

    // load all shared libraries
    for (int i = 0, n = cfg().load_shared_library_size(); i < n; ++i)
    {
        glog.is(DEBUG1) && glog << "Loading shared library: " << cfg().load_shared_library(i)
                                << std::endl;

        void* handle =
            dccl::DynamicProtobufManager::load_from_shared_lib(cfg().load_shared_library(i));

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
    for (int i = 0, n = cfg().load_proto_file_size(); i < n; ++i)
    {
        glog.is(DEBUG1) && glog << "Loading protobuf file: " << cfg().load_proto_file(i)
                                << std::endl;

        if (!dccl::DynamicProtobufManager::load_from_proto_file(cfg().load_proto_file(i)))
            glog.is(DIE) && glog << "Failed to load file." << std::endl;
    }

    r_manager_.set_cfg(cfg().route_cfg());
    q_managers_.resize(cfg().subnet_size());
    mac_managers_.resize(cfg().subnet_size());
    for (int i = 0, n = cfg().subnet_size(); i < n; ++i)
    {
        q_managers_[i].reset(new goby::acomms::QueueManager);
        mac_managers_[i].reset(new goby::acomms::MACManager);

        goby::acomms::protobuf::QueueManagerConfig qcfg = cfg().subnet(i).queue_cfg();
        q_managers_[i]->set_cfg(qcfg);

        mac_managers_[i]->startup(cfg().subnet(i).mac_cfg());

        goby::acomms::bind(*q_managers_[i], r_manager_);

        goby::acomms::connect(&(q_managers_[i]->signal_ack),
                              std::bind(&Bridge::handle_link_ack, this, std::placeholders::_1,
                                        std::placeholders::_2, q_managers_[i].get()));

        goby::acomms::connect(&(q_managers_[i]->signal_receive),
                              std::bind(&Bridge::handle_queue_receive, this, std::placeholders::_1,
                                        q_managers_[i].get()));

        subscribe_groups_.insert(std::make_pair(qcfg.modem_id(), SubscribeGroups(qcfg.modem_id())));

        interprocess().subscribe_dynamic<goby::acomms::protobuf::ModemTransmission>(
            std::bind(&Bridge::handle_modem_receive, this, std::placeholders::_1,
                      q_managers_[i].get()),
            subscribe_groups_.at(qcfg.modem_id()).rx);

        interprocess().subscribe_type_regex<google::protobuf::Message>(
            std::bind(&Bridge::handle_external_push, this, std::placeholders::_1,
                      q_managers_[i].get()),
            subscribe_groups_.at(qcfg.modem_id()).queue_push);

        interprocess().subscribe_dynamic<goby::acomms::protobuf::ModemTransmission>(
            std::bind(&Bridge::handle_data_request, this, std::placeholders::_1, i),
            subscribe_groups_.at(qcfg.modem_id()).data_request);

        interprocess().subscribe_dynamic<goby::acomms::protobuf::ModemDriverStatus>(
            std::bind(&Bridge::handle_driver_status, this, std::placeholders::_1, i),
            subscribe_groups_.at(qcfg.modem_id()).status);

        goby::acomms::connect(
            &mac_managers_[i]->signal_initiate_transmission,
            std::bind(&Bridge::handle_initiate_transmission, this, std::placeholders::_1, i));
    }
}

goby::apps::zeromq::acomms::Bridge::~Bridge() {}

void goby::apps::zeromq::acomms::Bridge::loop()
{
    for (std::vector<boost::shared_ptr<goby::acomms::QueueManager>>::iterator
             it = q_managers_.begin(),
             end = q_managers_.end();
         it != end; ++it)
    {
        (*it)->do_work();
    }

    for (std::vector<boost::shared_ptr<goby::acomms::MACManager>>::iterator
             it = mac_managers_.begin(),
             end = mac_managers_.end();
         it != end; ++it)
    {
        (*it)->do_work();
    }

    std::uint64_t now = goby::time::SystemClock::now<goby::time::MicroTime>().value();
    if (pending_hw_ctl_ && (pending_hw_ctl_->time() + cfg().special_command_ttl() * 1000000 < now))
    {
        glog.is(VERBOSE) && glog << "HardwareControlCommand expired." << std::endl;

        generate_hw_ctl_network_ack(q_managers_.at(0).get(),
                                    goby::acomms::protobuf::NetworkAck::EXPIRE);
        pending_hw_ctl_.reset();
    }

    if (pending_time_update_ &&
        (pending_time_update_->time() + cfg().special_command_ttl() * 1000000 < now))
    {
        glog.is(VERBOSE) && glog << "TimeUpdateRequest expired." << std::endl;

        generate_time_update_network_ack(q_managers_.at(0).get(),
                                         goby::acomms::protobuf::NetworkAck::EXPIRE);
        pending_time_update_.reset();
    }
}

void goby::apps::zeromq::acomms::Bridge::handle_queue_receive(
    const google::protobuf::Message& msg, goby::acomms::QueueManager* from_queue)
{
    interprocess().publish_dynamic(
        msg, goby::middleware::DynamicGroup(goby::middleware::acomms::groups::queue_rx,
                                            from_queue->modem_id()));

    // handle various command messages
    if (msg.GetDescriptor() == goby::acomms::protobuf::RouteCommand::descriptor())
    {
        goby::acomms::protobuf::RouteCommand route_cmd;
        route_cmd.CopyFrom(msg);
        glog.is(VERBOSE) && glog << "Received RouteCommand: " << msg.DebugString() << std::endl;
        goby::acomms::protobuf::RouteManagerConfig rt_cfg = cfg().route_cfg();
        rt_cfg.mutable_route()->CopyFrom(route_cmd.new_route());
        r_manager_.set_cfg(rt_cfg);
    }
    else if (msg.GetDescriptor() ==
             goby::acomms::micromodem::protobuf::HardwareControlCommand::descriptor())
    {
        pending_hw_ctl_.reset(new goby::acomms::micromodem::protobuf::HardwareControlCommand);
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

void goby::apps::zeromq::acomms::Bridge::handle_link_ack(
    const goby::acomms::protobuf::ModemTransmission& ack_msg,
    const google::protobuf::Message& orig_msg, goby::acomms::QueueManager* from_queue)
{
    // publish link ack
    interprocess().publish_dynamic(
        orig_msg, goby::middleware::DynamicGroup(goby::middleware::acomms::groups::queue_ack_orig,
                                                 from_queue->modem_id()));
}

void goby::apps::zeromq::acomms::Bridge::handle_modem_receive(
    const goby::acomms::protobuf::ModemTransmission& message, goby::acomms::QueueManager* in_queue)
{
    try
    {
        in_queue->handle_modem_receive(message);

        if (cfg().forward_cacst())
        {
            for (int i = 0,
                     n = message.GetExtension(goby::acomms::micromodem::protobuf::transmission)
                             .receive_stat_size();
                 i < n; ++i)
            {
                goby::acomms::micromodem::protobuf::ReceiveStatistics cacst =
                    message.GetExtension(goby::acomms::micromodem::protobuf::transmission)
                        .receive_stat(i);

                glog.is(VERBOSE) && glog << "Forwarding statistics message to topside: "
                                         << cacst.ShortDebugString() << std::endl;
                r_manager_.handle_in(in_queue->meta_from_msg(cacst), cacst, in_queue->modem_id());
            }
        }

        if (cfg().forward_ranging_reply() &&
            message.GetExtension(goby::acomms::micromodem::protobuf::transmission)
                .has_ranging_reply())
        {
            goby::acomms::micromodem::protobuf::RangingReply ranging =
                message.GetExtension(goby::acomms::micromodem::protobuf::transmission)
                    .ranging_reply();

            glog.is(VERBOSE) && glog << "Forwarding ranging message to topside: "
                                     << ranging.ShortDebugString() << std::endl;
            r_manager_.handle_in(in_queue->meta_from_msg(ranging), ranging, in_queue->modem_id());
        }

        if (pending_time_update_)
        {
            if (message.type() == goby::acomms::protobuf::ModemTransmission::DRIVER_SPECIFIC &&
                message.GetExtension(goby::acomms::micromodem::protobuf::transmission).type() ==
                    goby::acomms::micromodem::protobuf::MICROMODEM_TWO_WAY_PING)
            {
                goby::acomms::micromodem::protobuf::RangingReply range_reply =
                    message.GetExtension(goby::acomms::micromodem::protobuf::transmission)
                        .ranging_reply();

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
            message.GetExtension(goby::acomms::micromodem::protobuf::transmission).type() ==
                goby::acomms::micromodem::protobuf::MICROMODEM_HARDWARE_CONTROL_REPLY)
        {
            goby::acomms::micromodem::protobuf::HardwareControl control =
                message.GetExtension(goby::acomms::micromodem::protobuf::transmission).hw_ctl();

            if (message.src() == pending_hw_ctl_->hw_ctl_dest() &&
                message.dest() == in_queue->modem_id())
            {
                glog.is(VERBOSE) &&
                    glog << "Received hardware control response: " << control.ShortDebugString()
                         << " to our command: " << pending_hw_ctl_->ShortDebugString() << std::endl;

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

void goby::apps::zeromq::acomms::Bridge::generate_hw_ctl_network_ack(
    goby::acomms::QueueManager* in_queue, goby::acomms::protobuf::NetworkAck::AckType type)
{
    goby::acomms::protobuf::NetworkAck ack;
    ack.set_ack_src(pending_hw_ctl_->hw_ctl_dest());
    ack.set_message_dccl_id(goby::acomms::DCCLCodec::get()->id(pending_hw_ctl_->GetDescriptor()));

    ack.set_message_src(pending_hw_ctl_->command_src());
    ack.set_message_dest(pending_hw_ctl_->command_dest());
    ack.set_message_time(pending_hw_ctl_->time());
    ack.set_ack_type(type);

    r_manager_.handle_in(in_queue->meta_from_msg(ack), ack, in_queue->modem_id());
}

void goby::apps::zeromq::acomms::Bridge::generate_time_update_network_ack(
    goby::acomms::QueueManager* in_queue, goby::acomms::protobuf::NetworkAck::AckType type)
{
    goby::acomms::protobuf::NetworkAck ack;
    ack.set_ack_src(pending_time_update_->dest());
    ack.set_message_dccl_id(goby::acomms::DCCLCodec::get()->id(
        goby::acomms::protobuf::TimeUpdateRequest::descriptor()));

    ack.set_message_src(pending_time_update_->request_src());
    ack.set_message_dest(pending_time_update_->dest());
    ack.set_message_time(time_update_request_time_);
    ack.set_ack_type(type);

    r_manager_.handle_in(in_queue->meta_from_msg(ack), ack, in_queue->modem_id());
}

void goby::apps::zeromq::acomms::Bridge::handle_initiate_transmission(
    const goby::acomms::protobuf::ModemTransmission& m, int subnet)
{
    // see if we need to override with a time update ping
    if (pending_time_update_ && (m.dest() == pending_time_update_->dest() ||
                                 m.dest() == goby::acomms::QUERY_DESTINATION_ID))
    {
        goby::acomms::protobuf::ModemTransmission new_transmission = m;
        if (!pending_time_update_->has_time_of_flight_microsec())
        {
            new_transmission.set_dest(pending_time_update_->dest());
            new_transmission.set_type(goby::acomms::protobuf::ModemTransmission::DRIVER_SPECIFIC);

            new_transmission.MutableExtension(goby::acomms::micromodem::protobuf::transmission)
                ->set_type(goby::acomms::micromodem::protobuf::MICROMODEM_TWO_WAY_PING);
        }
        else
        {
            // send it out!
            new_transmission.set_type(goby::acomms::protobuf::ModemTransmission::DATA);
            new_transmission.set_ack_requested(true);
            new_transmission.set_dest(pending_time_update_->dest());

            pending_time_update_->set_time(
                goby::time::SystemClock::now<goby::time::MicroTime>().value());

            goby::acomms::DCCLCodec::get()->encode(new_transmission.add_frame(),
                                                   *pending_time_update_);
        }
        interprocess().publish_dynamic(
            new_transmission,
            goby::middleware::DynamicGroup(goby::middleware::acomms::groups::tx,
                                           cfg().subnet(subnet).queue_cfg().modem_id()));
    }
    // see if we need to override with a hardware control command
    else if (pending_hw_ctl_ && (m.dest() == pending_hw_ctl_->hw_ctl_dest() ||
                                 m.dest() == goby::acomms::QUERY_DESTINATION_ID))
    {
        goby::acomms::protobuf::ModemTransmission new_transmission = m;
        new_transmission.set_dest(pending_hw_ctl_->hw_ctl_dest());
        new_transmission.set_type(goby::acomms::protobuf::ModemTransmission::DRIVER_SPECIFIC);
        new_transmission.MutableExtension(goby::acomms::micromodem::protobuf::transmission)
            ->set_type(goby::acomms::micromodem::protobuf::MICROMODEM_HARDWARE_CONTROL);

        *new_transmission.MutableExtension(goby::acomms::micromodem::protobuf::transmission)
             ->mutable_hw_ctl() = pending_hw_ctl_->control();

        interprocess().publish_dynamic(
            new_transmission,
            goby::middleware::DynamicGroup(goby::middleware::acomms::groups::tx,
                                           cfg().subnet(subnet).queue_cfg().modem_id()));
    }
    else
    {
        interprocess().publish_dynamic(
            m, goby::middleware::DynamicGroup(goby::middleware::acomms::groups::tx,
                                              cfg().subnet(subnet).queue_cfg().modem_id()));
    }
}

void goby::apps::zeromq::acomms::Bridge::handle_data_request(
    const goby::acomms::protobuf::ModemTransmission& orig_msg, int subnet)
{
    goby::acomms::protobuf::ModemTransmission msg = orig_msg;
    q_managers_[subnet]->handle_modem_data_request(&msg);

    interprocess().publish_dynamic(
        msg, goby::middleware::DynamicGroup(goby::middleware::acomms::groups::data_response,
                                            cfg().subnet(subnet).queue_cfg().modem_id()));
}

void goby::apps::zeromq::acomms::Bridge::handle_driver_status(
    const goby::acomms::protobuf::ModemDriverStatus& m, int subnet)
{
    glog.is(VERBOSE) && glog << "Forwarding modemdriver status message to topside: "
                             << m.ShortDebugString() << std::endl;
    goby::acomms::QueueManager* in_queue = q_managers_[subnet].get();

    r_manager_.handle_in(in_queue->meta_from_msg(m), m, in_queue->modem_id());
}
