// Copyright 2016-2026:
//   GobySoft, LLC (2013-)
//   Community contributors (see AUTHORS file)
// File authors:
//   Toby Schneider <toby@gobysoft.org>
//   Copilot <198982749+Copilot@users.noreply.github.com>
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

#ifndef GOBY_ZEROMQ_TRANSPORT_INTERPROCESS_H
#define GOBY_ZEROMQ_TRANSPORT_INTERPROCESS_H

#include "goby/middleware/marshalling/protobuf.h"

#include <atomic>             // for atomic
#include <chrono>             // for mill...
#include <condition_variable> // for cond...
#include <deque>              // for deque
#include <functional>         // for func...
#include <iosfwd>             // for size_t
#include <memory>             // for shar...
#include <mutex>              // for time...
#include <set>                // for set
#include <string>             // for string
#include <thread>             // for get_id
#include <tuple>              // for make...
#include <unordered_map>      // for unor...
#include <utility>            // for make...
#include <vector>             // for vector

#include <zmq.h>   // for ZMQ_...
#include <zmq.hpp> // for sock...

#include "goby/middleware/common.h"                             // for thre...
#include "goby/middleware/group.h"                              // for Group
#include "goby/middleware/marshalling/interface.h"              // for Seri...
#include "goby/middleware/protobuf/serializer_transporter.pb.h" // for Seri...
#include "goby/middleware/protobuf/transporter_config.pb.h"     // for Tran...
#include "goby/middleware/transport/interface.h"                // for Poll...
#include "goby/middleware/transport/interprocess.h"             // for Inte...
#include "goby/middleware/transport/null.h"                     // for Null...
#include "goby/middleware/transport/serialization_handlers.h"   // for Seri...
#include "goby/middleware/transport/subscriber.h"               // for Subs...
#include "goby/time/system_clock.h"                             // for Syst...
#include "goby/util/debug_logger/flex_ostream.h"                // for Flex...
#include "goby/util/debug_logger/flex_ostreambuf.h"             // for lock
#include "goby/zeromq/protobuf/interprocess_config.pb.h"        // for Inte...
#include "goby/zeromq/protobuf/interprocess_zeromq.pb.h"        // for Inpr...

#if ZMQ_VERSION <= ZMQ_MAKE_VERSION(4, 3, 1)
#define USE_OLD_ZMQ_CPP_API
#endif

#if CPPZMQ_VERSION < ZMQ_MAKE_VERSION(4, 7, 1)
#define USE_OLD_CPPZMQ_SETSOCKOPT
#endif

#if CPPZMQ_VERSION < ZMQ_MAKE_VERSION(4, 8, 0)
#define USE_OLD_CPPZMQ_POLL
#endif

namespace goby
{
namespace middleware
{
template <typename Data> class Publisher;
} // namespace middleware

namespace zeromq
{
namespace groups
{
constexpr goby::middleware::Group manager_request{"goby::zeromq::_internal_manager_request"};
constexpr goby::middleware::Group manager_response{"goby::zeromq::_internal_manager_response"};
} // namespace groups

constexpr const char* delimiter_str{"/"};

void setup_socket(zmq::socket_t& socket, const protobuf::Socket& cfg);

#ifdef USE_OLD_ZMQ_CPP_API
using zmq_recv_flags_type = int;
using zmq_send_flags_type = int;
#else
using zmq_recv_flags_type = zmq::recv_flags;
using zmq_send_flags_type = zmq::send_flags;
#endif

// run in the same thread as InterProcessPortal
class InterProcessPortalMainThread
{
  public:
    InterProcessPortalMainThread(zmq::context_t& context);
    ~InterProcessPortalMainThread()
    {
#ifdef USE_OLD_CPPZMQ_SETSOCKOPT
        control_socket_.setsockopt(ZMQ_LINGER, 0);
        publish_socket_.setsockopt(ZMQ_LINGER, 0);
#else
        control_socket_.set(zmq::sockopt::linger, 0);
        publish_socket_.set(zmq::sockopt::linger, 0);
#endif
    }

    bool publish_ready() { return !hold_; }
    bool subscribe_ready() { return have_pubsub_sockets_; }

    bool recv(protobuf::InprocControl* control_msg,
              zmq_recv_flags_type flags = zmq_recv_flags_type());
    void set_publish_cfg(const protobuf::Socket& cfg);

    void set_hold_state(bool hold);
    bool hold_state() { return hold_; }

    void publish(const std::string& identifier, const char* bytes, int size,
                 bool ignore_buffer = false);
    void subscribe(const std::string& identifier);
    void unsubscribe(const std::string& identifier);
    void reader_shutdown();

    std::deque<protobuf::InprocControl>& control_buffer() { return control_buffer_; }
    void send_control_msg(const protobuf::InprocControl& control);

  private:
  private:
    zmq::socket_t control_socket_;
    zmq::socket_t publish_socket_;
    bool hold_{true};
    bool have_pubsub_sockets_{false};

    std::deque<std::pair<std::string, std::vector<char>>>
        publish_queue_; //used before hold == false

    // buffer messages while waiting for (un)subscribe ack
    std::deque<protobuf::InprocControl> control_buffer_;
};

// run in a separate thread to allow zmq_.poll() to block without interrupting the main thread
class InterProcessPortalReadThread
{
  public:
    InterProcessPortalReadThread(const protobuf::InterProcessPortalConfig& cfg,
                                 zmq::context_t& context, std::atomic<bool>& alive,
                                 std::shared_ptr<std::condition_variable> poller_cv);
    void run();
    ~InterProcessPortalReadThread()
    {
#ifdef USE_OLD_CPPZMQ_SETSOCKOPT
        control_socket_.setsockopt(ZMQ_LINGER, 0);
        subscribe_socket_.setsockopt(ZMQ_LINGER, 0);
        manager_socket_.setsockopt(ZMQ_LINGER, 0);
#else
        control_socket_.set(zmq::sockopt::linger, 0);
        subscribe_socket_.set(zmq::sockopt::linger, 0);
        manager_socket_.set(zmq::sockopt::linger, 0);
#endif
    }

  private:
    void poll(long timeout_ms = -1);
    void control_data(const zmq::message_t& zmq_msg);
    void subscribe_data(const zmq::message_t& zmq_msg);
    void manager_data(const zmq::message_t& zmq_msg);
    void send_control_msg(const protobuf::InprocControl& control);
    void send_manager_request(const protobuf::ManagerRequest& req);

  private:
    const protobuf::InterProcessPortalConfig& cfg_;
    zmq::socket_t control_socket_;
    zmq::socket_t subscribe_socket_;
    zmq::socket_t manager_socket_;
    std::atomic<bool>& alive_;
    std::shared_ptr<std::condition_variable> poller_cv_;
    std::vector<zmq::pollitem_t> poll_items_;
    enum
    {
        SOCKET_CONTROL = 0,
        SOCKET_MANAGER = 1,
        SOCKET_SUBSCRIBE = 2
    };
    enum
    {
        NUMBER_SOCKETS = 3
    };
    bool have_pubsub_sockets_{false};
    bool hold_{true};
    bool manager_waiting_for_reply_{false};

    goby::time::SystemClock::time_point next_hold_state_request_time_{
        goby::time::SystemClock::now()};
    const goby::time::SystemClock::duration hold_state_request_period_{
        std::chrono::milliseconds(100)};
};

template <typename InnerTransporter,
          template <typename Derived, typename InnerTransporterType> class PortalBase>
class InterProcessPortalImplementation
    : public PortalBase<InterProcessPortalImplementation<InnerTransporter, PortalBase>,
                        InnerTransporter>
{
  public:
    using Base = PortalBase<InterProcessPortalImplementation<InnerTransporter, PortalBase>,
                            InnerTransporter>;
    using IdentifierWildcard = middleware::IdentifierWildcard;

    InterProcessPortalImplementation(const protobuf::InterProcessPortalConfig& cfg)
        : cfg_(cfg),
          zmq_context_(cfg.zeromq_number_io_threads()),
          zmq_main_(zmq_context_),
          zmq_read_thread_(cfg_, zmq_context_, zmq_alive_, middleware::PollerInterface::cv())
    {
        _init();
    }

    InterProcessPortalImplementation(InnerTransporter& inner,
                                     const protobuf::InterProcessPortalConfig& cfg)
        : Base(inner),
          cfg_(cfg),
          zmq_context_(cfg.zeromq_number_io_threads()),
          zmq_main_(zmq_context_),
          zmq_read_thread_(cfg_, zmq_context_, zmq_alive_, middleware::PollerInterface::cv())
    {
        _init();
    }

    ~InterProcessPortalImplementation()
    {
        if (zmq_thread_)
        {
            zmq_main_.reader_shutdown();
            zmq_thread_->join();
        }
    }

    /// \brief When using hold functionality, call when the process is ready to receive publications (typically done after most or all subscribe calls)
    void ready() { ready_ = true; }

    /// \brief When using hold functionality, returns whether the system is holding (true) and thus waiting for all processes to connect and be ready, or running (false).
    bool hold_state() { return zmq_main_.hold_state(); }

    friend Base;
    friend typename Base::Base;
    friend typename Base::Common;

  private:
    void _init()
    {
        goby::glog.set_lock_action(goby::util::logger_lock::lock);

        // start zmq read thread
        zmq_thread_ = std::make_unique<std::thread>([this]() { zmq_read_thread_.run(); });

        while (!zmq_main_.subscribe_ready())
        {
            protobuf::InprocControl control_msg;
            if (zmq_main_.recv(&control_msg))
            {
                switch (control_msg.type())
                {
                    case protobuf::InprocControl::PUB_CONFIGURATION:
                        zmq_main_.set_publish_cfg(control_msg.publish_socket());
                        break;
                    default: break;
                }
            }
        }

        //
        // Handle hold state request/response using pub sub so that we ensure
        // publishing and subscribe is completely functional before releasing the hold
        //
        this->template _subscribe<protobuf::ManagerResponse,
                                  middleware::MarshallingScheme::PROTOBUF>(
            [this](std::shared_ptr<const protobuf::ManagerResponse> response)
            {
                goby::glog.is_debug3() && goby::glog << "Received ManagerResponse: "
                                                     << response->ShortDebugString() << std::endl;
                if (response->request() == protobuf::PROVIDE_HOLD_STATE &&
                    response->client_pid() == getpid() &&
                    response->client_name() == cfg_.client_name())
                {
                    zmq_main_.set_hold_state(response->hold());
                }

                // we're good to go now, so let's unsubscribe to this group
                if (zmq_main_.publish_ready())
                {
                    this->template _unsubscribe<protobuf::ManagerResponse,
                                                middleware::MarshallingScheme::PROTOBUF>(
                        groups::manager_response,
                        middleware::Subscriber<protobuf::ManagerResponse>());
                }
            },
            groups::manager_response, middleware::Subscriber<protobuf::ManagerResponse>());
    }

    void _do_publish(const std::string& identifier, const std::vector<char>& bytes)
    {
        zmq_main_.publish(identifier, &bytes[0], bytes.size(), ignore_buffer_);
    }

    void _do_portal_subscribe(const std::string& identifier) { zmq_main_.subscribe(identifier); }
    void _do_portal_unsubscribe(const std::string& identifier)
    {
        zmq_main_.unsubscribe(identifier);
    }

    void _do_portal_wildcard_subscribe() { zmq_main_.subscribe(delimiter_str); }
    void _do_portal_wildcard_unsubscribe() { zmq_main_.unsubscribe(delimiter_str); }

    int _poll(std::unique_ptr<std::unique_lock<std::mutex>>& lock)
    {
        int items = 0;
        protobuf::InprocControl new_control_msg;

#ifdef USE_OLD_ZMQ_CPP_API
        int flags = ZMQ_NOBLOCK;
#else
        auto flags = zmq::recv_flags::dontwait;
#endif

        while (zmq_main_.recv(&new_control_msg, flags))
            zmq_main_.control_buffer().push_back(new_control_msg);

        while (!zmq_main_.control_buffer().empty())
        {
            const auto& control_msg = zmq_main_.control_buffer().front();
            switch (control_msg.type())
            {
                case protobuf::InprocControl::RECEIVE:
                {
                    ++items;
                    this->_handle_received_data(lock, control_msg.received_data());
                }
                break;

                case protobuf::InprocControl::REQUEST_HOLD_STATE:
                {
                    protobuf::ManagerRequest req;

                    req.set_ready(ready_);
                    req.set_request(protobuf::PROVIDE_HOLD_STATE);
                    req.set_client_name(cfg_.client_name());
                    req.set_client_pid(getpid());

                    goby::glog.is_debug3() && goby::glog << "Published ManagerRequest: "
                                                         << req.ShortDebugString() << std::endl;

                    ignore_buffer_ = true;
                    this->template publish<groups::manager_request>(req);
                    ignore_buffer_ = false;
                }
                break;

                default: break;
            }
            zmq_main_.control_buffer().pop_front();
        }
        return items;
    }

  private:
    const protobuf::InterProcessPortalConfig cfg_;

    std::unique_ptr<std::thread> zmq_thread_;
    std::atomic<bool> zmq_alive_{true};
    zmq::context_t zmq_context_;
    InterProcessPortalMainThread zmq_main_;
    InterProcessPortalReadThread zmq_read_thread_;

    bool ready_{false};
    bool ignore_buffer_{false};
};

class Router
{
  public:
    Router(zmq::context_t& context, const protobuf::InterProcessPortalConfig& cfg)
        : context_(context), cfg_(cfg)
    {
    }

    void run();
    unsigned last_port(zmq::socket_t& socket);

    Router(Router&) = delete;
    Router& operator=(Router&) = delete;

  public:
    std::atomic<unsigned> pub_port{0};
    std::atomic<unsigned> sub_port{0};

  private:
    zmq::context_t& context_;
    const protobuf::InterProcessPortalConfig& cfg_;
};

class Manager
{
  public:
    Manager(zmq::context_t& context, const protobuf::InterProcessPortalConfig& cfg,
            const Router& router);

    Manager(zmq::context_t& context, const protobuf::InterProcessPortalConfig& cfg,
            const Router& router, const protobuf::InterProcessManagerHold& hold)
        : Manager(context, cfg, router)
    {
        for (const auto& req_c : hold.required_client()) required_clients_.insert(req_c);
    }

    void run();

    protobuf::ManagerResponse handle_request(const protobuf::ManagerRequest& pb_request);
    protobuf::Socket publish_socket_cfg();
    protobuf::Socket subscribe_socket_cfg();

    bool hold_state();

  private:
    std::set<std::string> reported_clients_;
    std::set<std::string> required_clients_;

    zmq::context_t& context_;
    const protobuf::InterProcessPortalConfig& cfg_;
    const Router& router_;

    std::vector<zmq::pollitem_t> poll_items_;
    enum
    {
        SOCKET_MANAGER = 0,
        SOCKET_SUBSCRIBE = 1,
    };
    enum
    {
        NUMBER_SOCKETS = 2
    };

    std::unique_ptr<zmq::socket_t> manager_socket_;
    std::unique_ptr<zmq::socket_t> subscribe_socket_;
    std::unique_ptr<zmq::socket_t> publish_socket_;

    std::string zmq_filter_req_{middleware::InterProcessIdentifierManager::make_identifier(
        middleware::SerializerParserHelper<
            protobuf::ManagerRequest, middleware::scheme<protobuf::ManagerRequest>()>::type_name(),
        middleware::scheme<protobuf::ManagerRequest>(), groups::manager_request,
        middleware::IdentifierWildcard::PROCESS_THREAD_WILDCARD, std::to_string(getpid()))};

    std::string zmq_filter_rep_{
        middleware::InterProcessIdentifierManager::make_identifier(
            middleware::SerializerParserHelper<
                protobuf::ManagerResponse,
                middleware::scheme<protobuf::ManagerResponse>()>::type_name(),
            middleware::scheme<protobuf::ManagerResponse>(), groups::manager_response,
            middleware::IdentifierWildcard::NO_WILDCARDS, std::to_string(getpid())) +
        std::string(1, middleware::InterProcessIdentifierManager::end_delimiter)};
}; // namespace zeromq

template <typename InnerTransporter = middleware::NullTransporter>
using InterProcessPortal =
    InterProcessPortalImplementation<InnerTransporter, middleware::InterProcessPortalBase>;

} // namespace zeromq
} // namespace goby

#endif
