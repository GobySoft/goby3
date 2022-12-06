// Copyright 2016-2022:
//   GobySoft, LLC (2013-)
//   Community contributors (see AUTHORS file)
// File authors:
//   Toby Schneider <toby@gobysoft.org>
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

#include <algorithm>   // for copy, max, copy_backward, equal, set_d...
#include <cstring>     // for memcpy, size_t
#include <ostream>     // for endl, basic_ostream, basic_ostream<>::...
#include <stdexcept>   // for runtime_error
#include <type_traits> // for __success_type<>::type
#include <utility>     // for pair, move

#include "interprocess.h"

#if GOOGLE_PROTOBUF_VERSION < 3001000
#define ByteSizeLong ByteSize
#endif

using goby::glog;
using namespace goby::util::logger;

// support moving to new API in ZeroMQ 4.3.1
#ifdef USE_OLD_ZMQ_CPP_API
int zmq_send_flags_none{0};
int zmq_recv_flags_none{0};
#else
auto zmq_send_flags_none{zmq::send_flags::none};
auto zmq_recv_flags_none{zmq::recv_flags::none};
#endif

bool zmq_socket_recv(zmq::socket_t& socket, zmq::message_t& msg,
                     goby::zeromq::zmq_recv_flags_type flags = zmq_recv_flags_none)
{
#ifdef USE_OLD_ZMQ_CPP_API
    return socket.recv(&msg, flags);
#else
    return bool(socket.recv(msg, flags));
#endif
}

void goby::zeromq::setup_socket(zmq::socket_t& socket, const protobuf::Socket& cfg)
{
    int send_hwm = cfg.send_queue_size();
    int receive_hwm = cfg.receive_queue_size();
    socket.setsockopt(ZMQ_SNDHWM, &send_hwm, sizeof(send_hwm));
    socket.setsockopt(ZMQ_RCVHWM, &receive_hwm, sizeof(receive_hwm));

    bool bind = (cfg.connect_or_bind() == protobuf::Socket::BIND);

    std::string endpoint;
    switch (cfg.transport())
    {
        case protobuf::Socket::IPC: endpoint = "ipc://" + cfg.socket_name(); break;
        case protobuf::Socket::TCP:
            endpoint = "tcp://" + (bind ? std::string("*") : cfg.ethernet_address()) + ":" +
                       std::to_string(cfg.ethernet_port());
            break;
        default:
            throw(std::runtime_error("Unsupported transport type: " +
                                     protobuf::Socket::Transport_Name(cfg.transport())));
            break;
    }

    if (bind)
        socket.bind(endpoint.c_str());
    else
        socket.connect(endpoint.c_str());
}

//
// InterProcessPortalMainThread
//

goby::zeromq::InterProcessPortalMainThread::InterProcessPortalMainThread(zmq::context_t& context)
    : control_socket_(context, ZMQ_PAIR), publish_socket_(context, ZMQ_PUB)
{
    control_socket_.bind("inproc://control");
}

bool goby::zeromq::InterProcessPortalMainThread::recv(protobuf::InprocControl* control_msg,
                                                      zmq_recv_flags_type flags)
{
    zmq::message_t zmq_msg;
    bool message_received = false;
    if (zmq_socket_recv(control_socket_, zmq_msg, flags))
    {
        control_msg->ParseFromArray((char*)zmq_msg.data(), zmq_msg.size());
        glog.is(DEBUG3) && glog << "Main thread received control msg: "
                                << control_msg->ShortDebugString() << std::endl;
        message_received = true;
    }

    return message_received;
}

void goby::zeromq::InterProcessPortalMainThread::set_publish_cfg(const protobuf::Socket& cfg)
{
    setup_socket(publish_socket_, cfg);
    have_pubsub_sockets_ = true;
}

void goby::zeromq::InterProcessPortalMainThread::set_hold_state(bool hold)
{
    // hold was on, and now it's off
    if (hold_ && !hold)
    {
        hold_ = hold;

        // TODO: this is necessary to allow initial ZMQ subscription forwarding
        // messages to flow through the system so that we don't lose the
        // first publication to subscriptions that happened before "hold: false"
        // Find some way to remove this delay!
        // Without this, we have problems losing the initial publications to
        // goby::middleware::intervehicle::modem_subscription_forward_tx from
        // the InterVehicle layer.
        // Can we do this by replacing the PUB socket with an XPUB and checking
        // for a subscriber?
        sleep(1);

        glog.is(DEBUG3) && glog << "InterProcessPortal**Main**Thread: Hold off" << std::endl;

        // publish any queued up messages
        for (auto& pub_pair : publish_queue_)
            publish(pub_pair.first, &pub_pair.second[0], pub_pair.second.size());
        publish_queue_.clear();
    }

    protobuf::InprocControl control;
    control.set_type(protobuf::InprocControl::NOTIFY_HOLD_STATE);
    control.set_hold(hold);
    send_control_msg(control);
}

void goby::zeromq::InterProcessPortalMainThread::publish(const std::string& identifier,
                                                         const char* bytes, int size,
                                                         bool ignore_buffer)
{
    if (publish_ready() || ignore_buffer)
    {
        zmq::message_t msg(identifier.size() + size);
        memcpy(msg.data(), identifier.data(), identifier.size());
        memcpy(static_cast<char*>(msg.data()) + identifier.size(), bytes, size);

        publish_socket_.send(msg, zmq_send_flags_none);

        glog.is(DEBUG3) && glog << "Published " << size << " bytes to ["
                                << identifier.substr(0, identifier.size() - 1) << "]" << std::endl;
    }
    else
    {
        glog.is(DEBUG3) && glog << "Buffering publication of " << size << " bytes to ["
                                << identifier.substr(0, identifier.size() - 1) << "]" << std::endl;

        publish_queue_.emplace_back(identifier, std::vector<char>(bytes, bytes + size));
    }
}

void goby::zeromq::InterProcessPortalMainThread::subscribe(const std::string& identifier)
{
    protobuf::InprocControl control;
    control.set_type(protobuf::InprocControl::SUBSCRIBE);
    control.set_subscription_identifier(identifier);
    send_control_msg(control);

    glog.is(DEBUG3) && glog << "Requesting subscribe for " << identifier << std::endl;

    // wait for ack
    protobuf::InprocControl control_msg;
    recv(&control_msg);
    while (control_msg.type() != protobuf::InprocControl::SUBSCRIBE_ACK)
    {
        control_buffer_.push_back(control_msg);
        recv(&control_msg);
    }
}

void goby::zeromq::InterProcessPortalMainThread::unsubscribe(const std::string& identifier)
{
    protobuf::InprocControl control;
    control.set_type(protobuf::InprocControl::UNSUBSCRIBE);
    control.set_subscription_identifier(identifier);
    send_control_msg(control);

    // wait for ack
    protobuf::InprocControl control_msg;
    recv(&control_msg);
    while (control_msg.type() != protobuf::InprocControl::UNSUBSCRIBE_ACK)
    {
        control_buffer_.push_back(control_msg);
        recv(&control_msg);
    }
}
void goby::zeromq::InterProcessPortalMainThread::reader_shutdown()
{
    protobuf::InprocControl control;
    control.set_type(protobuf::InprocControl::SHUTDOWN);
    send_control_msg(control);
}

void goby::zeromq::InterProcessPortalMainThread::send_control_msg(
    const protobuf::InprocControl& control)
{
    zmq::message_t zmq_control_msg(control.ByteSizeLong());
    control.SerializeToArray((char*)zmq_control_msg.data(), zmq_control_msg.size());

    control_socket_.send(zmq_control_msg, zmq_send_flags_none);
}

//
// InterProcessPortalReadThread
//
goby::zeromq::InterProcessPortalReadThread::InterProcessPortalReadThread(
    const protobuf::InterProcessPortalConfig& cfg, zmq::context_t& context,
    std::atomic<bool>& alive, std::shared_ptr<std::condition_variable_any> poller_cv)
    : cfg_(cfg),
      control_socket_(context, ZMQ_PAIR),
      subscribe_socket_(context, ZMQ_SUB),
      manager_socket_(context, ZMQ_REQ),
      alive_(alive),
      poller_cv_(std::move(poller_cv))
{
    poll_items_.resize(NUMBER_SOCKETS);
    poll_items_[SOCKET_CONTROL] = {(void*)control_socket_, 0, ZMQ_POLLIN, 0};
    poll_items_[SOCKET_MANAGER] = {(void*)manager_socket_, 0, ZMQ_POLLIN, 0};
    poll_items_[SOCKET_SUBSCRIBE] = {(void*)subscribe_socket_, 0, ZMQ_POLLIN, 0};

    control_socket_.connect("inproc://control");

    protobuf::Socket query_socket;
    query_socket.set_socket_type(protobuf::Socket::REQUEST);
    query_socket.set_socket_id(SOCKET_MANAGER);

    switch (cfg_.transport())
    {
        case protobuf::InterProcessPortalConfig::IPC:
            query_socket.set_transport(protobuf::Socket::IPC);
            query_socket.set_socket_name(
                (cfg_.has_socket_name() ? cfg_.socket_name() : "/tmp/goby_" + cfg_.platform()) +
                ".manager");
            break;
        case protobuf::InterProcessPortalConfig::TCP:
            query_socket.set_transport(protobuf::Socket::TCP);
            query_socket.set_ethernet_address(cfg_.ipv4_address());
            query_socket.set_ethernet_port(cfg_.tcp_port());
            break;
    }
    query_socket.set_connect_or_bind(protobuf::Socket::CONNECT);
    setup_socket(manager_socket_, query_socket);
}

void goby::zeromq::InterProcessPortalReadThread::run()
{
    while (alive_)
    {
        if (have_pubsub_sockets_ && !hold_)
        {
            poll();
        }
        else
        {
            protobuf::ManagerRequest req;
            if (!have_pubsub_sockets_)
            {
                req.set_request(protobuf::PROVIDE_PUB_SUB_SOCKETS);
                req.set_client_name(cfg_.client_name());
                req.set_client_pid(getpid());

                send_manager_request(req);

                auto start = goby::time::SystemClock::now();
                while (!have_pubsub_sockets_ &&
                       (start + std::chrono::seconds(cfg_.manager_timeout_seconds()) >
                        goby::time::SystemClock::now()))
                    poll(cfg_.manager_timeout_seconds() * 1000);

                if (!have_pubsub_sockets_)
                    goby::glog.is(goby::util::logger::DIE) &&
                        goby::glog << "No response from gobyd: " << cfg_.ShortDebugString()
                                   << std::endl;
            }
            else if (hold_ && goby::time::SystemClock::now() >= next_hold_state_request_time_)
            {
                goby::glog.is_debug3() &&
                    goby::glog << "InterProcessPortalReadThread requesting hold state" << std::endl;
                protobuf::InprocControl control;
                control.set_type(protobuf::InprocControl::REQUEST_HOLD_STATE);
                send_control_msg(control);
                next_hold_state_request_time_ =
                    goby::time::SystemClock::now() + hold_state_request_period_;
            }
            else
            {
                poll(10);
            }
        }
    }
}

void goby::zeromq::InterProcessPortalReadThread::send_manager_request(
    const protobuf::ManagerRequest& req)
{
    zmq::message_t msg(req.ByteSizeLong());
    req.SerializeToArray(static_cast<char*>(msg.data()), req.ByteSizeLong());
    manager_socket_.send(msg, zmq_send_flags_none);
    manager_waiting_for_reply_ = true;
}

void goby::zeromq::InterProcessPortalReadThread::poll(long timeout_ms)
{
    zmq::poll(&poll_items_[0], poll_items_.size(), timeout_ms);

    for (int i = 0, n = poll_items_.size(); i < n; ++i)
    {
        if (poll_items_[i].revents & ZMQ_POLLIN)
        {
            zmq::message_t zmq_msg;
            switch (i)
            {
                case SOCKET_CONTROL:
                    if (zmq_socket_recv(control_socket_, zmq_msg))
                        control_data(zmq_msg);
                    break;
                case SOCKET_SUBSCRIBE:
                    if (zmq_socket_recv(subscribe_socket_, zmq_msg))
                        subscribe_data(zmq_msg);
                    break;
                case SOCKET_MANAGER:
                    if (zmq_socket_recv(manager_socket_, zmq_msg))
                        manager_data(zmq_msg);
                    break;
            }
        }
    }
}

void goby::zeromq::InterProcessPortalReadThread::control_data(const zmq::message_t& zmq_msg)
{
    // command from the main thread
    protobuf::InprocControl control_msg;
    control_msg.ParseFromArray((char*)zmq_msg.data(), zmq_msg.size());

    switch (control_msg.type())
    {
        case protobuf::InprocControl::SUBSCRIBE:
        {
            auto& zmq_filter = control_msg.subscription_identifier();
            subscribe_socket_.setsockopt(ZMQ_SUBSCRIBE, zmq_filter.c_str(), zmq_filter.size());

            glog.is(DEBUG2) && glog << "subscribed with identifier: [" << zmq_filter << "]"
                                    << std::endl;

            protobuf::InprocControl control_ack;
            control_ack.set_type(protobuf::InprocControl::SUBSCRIBE_ACK);
            send_control_msg(control_ack);

            break;
        }
        case protobuf::InprocControl::UNSUBSCRIBE:
        {
            auto& zmq_filter = control_msg.subscription_identifier();
            glog.is(DEBUG2) && glog << "unsubscribing with identifier: [" << zmq_filter << "]"
                                    << std::endl;

            subscribe_socket_.setsockopt(ZMQ_UNSUBSCRIBE, zmq_filter.c_str(), zmq_filter.size());

            protobuf::InprocControl control_ack;
            control_ack.set_type(protobuf::InprocControl::UNSUBSCRIBE_ACK);
            send_control_msg(control_ack);

            break;
        }
        case protobuf::InprocControl::SHUTDOWN:
        {
            alive_ = false;
            break;
        }
        case protobuf::InprocControl::NOTIFY_HOLD_STATE:
        {
            if (hold_ && !control_msg.hold())
                glog.is(DEBUG3) && glog << "InterProcessPortal**Read**Thread: Hold off"
                                        << std::endl;
            hold_ = control_msg.hold();
            break;
        }

        default: break;
    }
}
void goby::zeromq::InterProcessPortalReadThread::subscribe_data(const zmq::message_t& zmq_msg)
{
    // data from goby - forward to the main thread
    protobuf::InprocControl control;
    control.set_type(protobuf::InprocControl::RECEIVE);
    control.set_received_data(std::string((char*)zmq_msg.data(), zmq_msg.size()));
    send_control_msg(control);
}
void goby::zeromq::InterProcessPortalReadThread::manager_data(const zmq::message_t& zmq_msg)
{
    // manager (gobyd) reply
    protobuf::ManagerResponse response;
    response.ParseFromArray(zmq_msg.data(), zmq_msg.size());

    glog.is(DEBUG3) && glog << "Received manager response: " << response.DebugString() << std::endl;

    if (response.request() == protobuf::PROVIDE_PUB_SUB_SOCKETS)
    {
        if (response.subscribe_socket().transport() == protobuf::Socket::TCP)
            response.mutable_subscribe_socket()->set_ethernet_address(cfg_.ipv4_address());
        if (response.publish_socket().transport() == protobuf::Socket::TCP)
            response.mutable_publish_socket()->set_ethernet_address(cfg_.ipv4_address());

        setup_socket(subscribe_socket_, response.subscribe_socket());

        protobuf::InprocControl control;
        control.set_type(protobuf::InprocControl::PUB_CONFIGURATION);
        control.set_hold(response.hold());
        *control.mutable_publish_socket() = response.publish_socket();
        send_control_msg(control);

        have_pubsub_sockets_ = true;
    }

    manager_waiting_for_reply_ = false;
}

void goby::zeromq::InterProcessPortalReadThread::send_control_msg(
    const protobuf::InprocControl& control)
{
    zmq::message_t zmq_control_msg(control.ByteSizeLong());
    control.SerializeToArray((char*)zmq_control_msg.data(), zmq_control_msg.size());
    control_socket_.send(zmq_control_msg, zmq_send_flags_none);
    poller_cv_->notify_all();
}

//
// Router
//

unsigned goby::zeromq::Router::last_port(zmq::socket_t& socket)
{
    size_t last_endpoint_size = 100;
    char last_endpoint[last_endpoint_size];
    int rc = zmq_getsockopt((void*)socket, ZMQ_LAST_ENDPOINT, &last_endpoint, &last_endpoint_size);

    if (rc != 0)
        throw(std::runtime_error("Could not retrieve ZMQ_LAST_ENDPOINT"));

    std::string last_ep(last_endpoint);
    unsigned port = std::stoi(last_ep.substr(last_ep.find_last_of(':') + 1));
    return port;
}

void goby::zeromq::Router::run()
{
    zmq::socket_t frontend(context_, ZMQ_XPUB);
    zmq::socket_t backend(context_, ZMQ_XSUB);

    int send_hwm = cfg_.send_queue_size();
    int receive_hwm = cfg_.receive_queue_size();
    frontend.setsockopt(ZMQ_SNDHWM, &send_hwm, sizeof(send_hwm));
    backend.setsockopt(ZMQ_SNDHWM, &send_hwm, sizeof(send_hwm));
    frontend.setsockopt(ZMQ_RCVHWM, &receive_hwm, sizeof(receive_hwm));
    backend.setsockopt(ZMQ_RCVHWM, &receive_hwm, sizeof(receive_hwm));

    switch (cfg_.transport())
    {
        case protobuf::InterProcessPortalConfig::IPC:
        {
            std::string xpub_sock_name =
                "ipc://" +
                (cfg_.has_socket_name() ? cfg_.socket_name() : "/tmp/goby_" + cfg_.platform()) +
                ".xpub";
            std::string xsub_sock_name =
                "ipc://" +
                (cfg_.has_socket_name() ? cfg_.socket_name() : "/tmp/goby_" + cfg_.platform()) +
                ".xsub";
            frontend.bind(xpub_sock_name.c_str());
            backend.bind(xsub_sock_name.c_str());
            break;
        }
        case protobuf::InterProcessPortalConfig::TCP:
        {
            frontend.bind("tcp://*:0");
            backend.bind("tcp://*:0");
            pub_port = last_port(frontend);
            sub_port = last_port(backend);
            break;
        }
    }
    try
    {
#ifdef USE_OLD_ZMQ_CPP_API
        zmq::proxy((void*)frontend, (void*)backend, nullptr);
#else
        zmq::proxy(frontend, backend);
#endif
    }
    catch (const zmq::error_t& e)
    {
        // context terminated
        if (e.num() == ETERM)
            return;
        else
            throw(e);
    }
}

//
// Manager
//

goby::zeromq::Manager::Manager(zmq::context_t& context,
                               const protobuf::InterProcessPortalConfig& cfg, const Router& router)
    : context_(context),
      cfg_(cfg),
      router_(router),
      manager_socket_(std::make_unique<zmq::socket_t>(context_, ZMQ_REP)),
      subscribe_socket_(std::make_unique<zmq::socket_t>(context_, ZMQ_SUB)),
      publish_socket_(std::make_unique<zmq::socket_t>(context_, ZMQ_PUB))
{
    setup_socket(*subscribe_socket_, subscribe_socket_cfg());
    setup_socket(*publish_socket_, publish_socket_cfg());
    poll_items_.resize(NUMBER_SOCKETS);
    poll_items_[SOCKET_MANAGER] = {(void*)*manager_socket_, 0, ZMQ_POLLIN, 0};
    poll_items_[SOCKET_SUBSCRIBE] = {(void*)*subscribe_socket_, 0, ZMQ_POLLIN, 0};

    subscribe_socket_->setsockopt(ZMQ_SUBSCRIBE, zmq_filter_req_.c_str(), zmq_filter_req_.size());

    switch (cfg_.transport())
    {
        case protobuf::InterProcessPortalConfig::IPC:
        {
            std::string sock_name =
                "ipc://" +
                ((cfg_.has_socket_name() ? cfg_.socket_name() : "/tmp/goby_" + cfg_.platform()) +
                 ".manager");
            manager_socket_->bind(sock_name.c_str());
            break;
        }
        case protobuf::InterProcessPortalConfig::TCP:
        {
            std::string sock_name = "tcp://*:" + std::to_string(cfg_.tcp_port());
            manager_socket_->bind(sock_name.c_str());
            break;
        }
    }
}

void goby::zeromq::Manager::run()
{
    try
    {
        while (true)
        {
            zmq::poll(&poll_items_[0], poll_items_.size(), -1);
            for (int i = 0, n = poll_items_.size(); i < n; ++i)
            {
                if (poll_items_[i].revents & ZMQ_POLLIN)
                {
                    zmq::message_t request;
                    switch (i)
                    {
                        case SOCKET_MANAGER:
                        {
                            zmq_socket_recv(*manager_socket_, request);

                            protobuf::ManagerRequest pb_request;
                            pb_request.ParseFromArray((char*)request.data(), request.size());

                            auto pb_response = handle_request(pb_request);

                            zmq::message_t reply(pb_response.ByteSizeLong());
                            pb_response.SerializeToArray((char*)reply.data(), reply.size());
                            manager_socket_->send(reply, zmq_send_flags_none);
                            break;
                        }
                        case SOCKET_SUBSCRIBE:
                            zmq_socket_recv(*subscribe_socket_, request);
                            protobuf::ManagerRequest pb_request;
                            auto null_delim_it =
                                std::find((char*)request.data(),
                                          (char*)request.data() + request.size(), '\0');
                            pb_request.ParseFromArray(null_delim_it + 1, (char*)request.data() +
                                                                             request.size() -
                                                                             (null_delim_it + 1));

                            auto pb_response = handle_request(pb_request);

                            glog.is_debug3() && glog << "Manager:: Sending response: "
                                                     << pb_response.DebugString() << std::endl;

                            auto size = pb_response.ByteSizeLong();
                            zmq::message_t reply(zmq_filter_rep_.size() + size);
                            memcpy(reply.data(), zmq_filter_rep_.data(), zmq_filter_rep_.size());
                            pb_response.SerializeToArray(
                                static_cast<char*>(reply.data()) + zmq_filter_rep_.size(), size);

                            publish_socket_->send(reply, zmq_send_flags_none);
                            break;
                    }
                }
            }
        }
    }
    catch (const zmq::error_t& e)
    {
        // context terminated
        if (e.num() == ETERM)
        {
            manager_socket_.reset();
            subscribe_socket_.reset();
            publish_socket_.reset();
            return;
        }
        else
        {
            throw(e);
        }
    }
}

goby::zeromq::protobuf::ManagerResponse
goby::zeromq::Manager::handle_request(const protobuf::ManagerRequest& pb_request)
{
    glog.is(DEBUG3) && glog << "(Manager) Received request: " << pb_request.DebugString()
                            << std::endl;

    protobuf::ManagerResponse pb_response;
    pb_response.set_request(pb_request.request());

    pb_response.set_client_name(pb_request.client_name());
    pb_response.set_client_pid(pb_request.client_pid());

    if (pb_request.request() == protobuf::PROVIDE_PUB_SUB_SOCKETS)
    {
        *pb_response.mutable_subscribe_socket() = subscribe_socket_cfg();
        *pb_response.mutable_publish_socket() = publish_socket_cfg();
    }
    else if (pb_request.request() == protobuf::PROVIDE_HOLD_STATE)
    {
        if (pb_request.ready() && required_clients_.count(pb_request.client_name()))
            reported_clients_.insert(pb_request.client_name());

        pb_response.set_hold(hold_state());
    }

    return pb_response;
}

goby::zeromq::protobuf::Socket goby::zeromq::Manager::publish_socket_cfg()
{
    protobuf::Socket publish_socket;

    while (cfg_.transport() == protobuf::InterProcessPortalConfig::TCP && (router_.sub_port == 0))
        usleep(1e4);

    publish_socket.set_socket_type(protobuf::Socket::PUBLISH);
    publish_socket.set_connect_or_bind(protobuf::Socket::CONNECT);

    publish_socket.set_send_queue_size(cfg_.send_queue_size());
    publish_socket.set_receive_queue_size(cfg_.receive_queue_size());

    switch (cfg_.transport())
    {
        case protobuf::InterProcessPortalConfig::IPC:
            publish_socket.set_transport(protobuf::Socket::IPC);
            publish_socket.set_socket_name(
                (cfg_.has_socket_name() ? cfg_.socket_name() : "/tmp/goby_" + cfg_.platform()) +
                ".xsub");
            break;
        case protobuf::InterProcessPortalConfig::TCP:
            publish_socket.set_transport(protobuf::Socket::TCP);
            publish_socket.set_ethernet_port(router_.sub_port);
            break;
    }
    return publish_socket;
}

goby::zeromq::protobuf::Socket goby::zeromq::Manager::subscribe_socket_cfg()
{
    while (cfg_.transport() == protobuf::InterProcessPortalConfig::TCP && (router_.pub_port == 0))
        usleep(1e4);

    protobuf::Socket subscribe_socket;

    subscribe_socket.set_socket_type(protobuf::Socket::SUBSCRIBE);
    subscribe_socket.set_connect_or_bind(protobuf::Socket::CONNECT);
    subscribe_socket.set_send_queue_size(cfg_.send_queue_size());
    subscribe_socket.set_receive_queue_size(cfg_.receive_queue_size());

    switch (cfg_.transport())
    {
        case protobuf::InterProcessPortalConfig::IPC:
            subscribe_socket.set_transport(protobuf::Socket::IPC);
            subscribe_socket.set_socket_name(
                (cfg_.has_socket_name() ? cfg_.socket_name() : "/tmp/goby_" + cfg_.platform()) +
                ".xpub");
            break;
        case protobuf::InterProcessPortalConfig::TCP:
            subscribe_socket.set_transport(protobuf::Socket::TCP);
            subscribe_socket.set_ethernet_port(router_.pub_port); // our publish is their subscribe
            break;
    }

    return subscribe_socket;
}

bool goby::zeromq::Manager::hold_state()
{
    bool hold = reported_clients_ != required_clients_;
    if (hold && goby::glog.is_debug3())
    {
        std::vector<std::string> missing(required_clients_.size());
        auto it = std::set_difference(required_clients_.begin(), required_clients_.end(),
                                      reported_clients_.begin(), reported_clients_.end(),
                                      missing.begin());
        missing.resize(it - missing.begin());

        goby::glog << "Hold on: waiting for: ";
        for (const auto& m : missing) goby::glog << m << ", ";
        goby::glog << std::endl;
    }
    return hold;
}
