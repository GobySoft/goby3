// Copyright 2016-2021:
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
#include <unistd.h>           // for getpid
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

void setup_socket(zmq::socket_t& socket, const protobuf::Socket& cfg);

enum class IdentifierWildcard
{
    NO_WILDCARDS,
    THREAD_WILDCARD,
    PROCESS_THREAD_WILDCARD
};

// scheme
inline std::string identifier_part_to_string(int i)
{
    return middleware::MarshallingScheme::to_string(i);
}
inline std::string identifier_part_to_string(std::thread::id i)
{
    return goby::middleware::thread_id(i);
}

/// Given key, find the string in the map, or create it (to_string) and store it, and return the string.
template <typename Key>
const std::string& id_component(const Key& k, std::unordered_map<Key, std::string>& map)
{
    auto it = map.find(k);
    if (it != map.end())
        return it->second;

    std::string v = identifier_part_to_string(k) + "/";
    auto it_pair = map.insert(std::make_pair(k, v));
    return it_pair.first->second;
}

inline std::string
make_identifier(const std::string& type_name, int scheme, const std::string& group,
                IdentifierWildcard wildcard, const std::string& process,
                std::unordered_map<int, std::string>* schemes_buffer = nullptr,
                std::unordered_map<std::thread::id, std::string>* threads_buffer = nullptr)
{
    switch (wildcard)
    {
        default:
        case IdentifierWildcard::NO_WILDCARDS:
        {
            auto thread = std::this_thread::get_id();
            return ("/" + group + "/" +
                    (schemes_buffer ? id_component(scheme, *schemes_buffer)
                                    : std::string(identifier_part_to_string(scheme) + "/")) +
                    type_name + "/" + process + "/" +
                    (threads_buffer ? id_component(thread, *threads_buffer)
                                    : std::string(identifier_part_to_string(thread) + "/")));
        }
        case IdentifierWildcard::THREAD_WILDCARD:
        {
            return ("/" + group + "/" +
                    (schemes_buffer ? id_component(scheme, *schemes_buffer)
                                    : std::string(identifier_part_to_string(scheme) + "/")) +
                    type_name + "/" + process + "/");
        }
        case IdentifierWildcard::PROCESS_THREAD_WILDCARD:
        {
            return ("/" + group + "/" +
                    (schemes_buffer ? id_component(scheme, *schemes_buffer)
                                    : std::string(identifier_part_to_string(scheme) + "/")) +
                    type_name + "/");
        }
    }
}

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
        control_socket_.setsockopt(ZMQ_LINGER, 0);
        publish_socket_.setsockopt(ZMQ_LINGER, 0);
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
                                 std::shared_ptr<std::condition_variable_any> poller_cv);
    void run();
    ~InterProcessPortalReadThread()
    {
        control_socket_.setsockopt(ZMQ_LINGER, 0);
        subscribe_socket_.setsockopt(ZMQ_LINGER, 0);
        manager_socket_.setsockopt(ZMQ_LINGER, 0);
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
    std::shared_ptr<std::condition_variable_any> poller_cv_;
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

                if (control_msg.has_hold())
                    zmq_main_.set_hold_state(control_msg.hold());
            }
        }

        //
        // Handle hold state request/response using pub sub so that we ensure
        // publishing and subscribe is completely functional before releasing the hold
        //
        _subscribe<protobuf::ManagerResponse, middleware::MarshallingScheme::PROTOBUF>(
            [this](std::shared_ptr<const protobuf::ManagerResponse> response) {
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
                    _unsubscribe<protobuf::ManagerResponse,
                                 middleware::MarshallingScheme::PROTOBUF>(
                        groups::manager_response,
                        middleware::Subscriber<protobuf::ManagerResponse>());
                }
            },
            groups::manager_response, middleware::Subscriber<protobuf::ManagerResponse>());
    }

    template <typename Data, int scheme>
    void _publish(const Data& d, const goby::middleware::Group& group,
                  const middleware::Publisher<Data>& /*publisher*/, bool ignore_buffer = false)
    {
        std::vector<char> bytes(middleware::SerializerParserHelper<Data, scheme>::serialize(d));
        std::string identifier = _make_fully_qualified_identifier<Data, scheme>(d, group) + '\0';
        zmq_main_.publish(identifier, &bytes[0], bytes.size(), ignore_buffer);
    }

    template <typename Data, int scheme>
    void _subscribe(std::function<void(std::shared_ptr<const Data> d)> f,
                    const goby::middleware::Group& group,
                    const middleware::Subscriber<Data>& /*subscriber*/)
    {
        std::string identifier =
            _make_identifier<Data, scheme>(group, IdentifierWildcard::PROCESS_THREAD_WILDCARD);

        auto subscription = std::make_shared<middleware::SerializationSubscription<Data, scheme>>(
            f, group,
            middleware::Subscriber<Data>(goby::middleware::protobuf::TransporterConfig(),
                                         [=](const Data& /*d*/) { return group; }));

        if (forwarder_subscriptions_.count(identifier) == 0 &&
            portal_subscriptions_.count(identifier) == 0)
            zmq_main_.subscribe(identifier);
        portal_subscriptions_.insert(std::make_pair(identifier, subscription));
    }

    void _subscribe_regex(
        std::function<void(const std::vector<unsigned char>&, int scheme, const std::string& type,
                           const goby::middleware::Group& group)>
            f,
        const std::set<int>& schemes, const std::string& type_regex, const std::string& group_regex)
    {
        auto new_sub = std::make_shared<middleware::SerializationSubscriptionRegex>(
            f, schemes, type_regex, group_regex);
        _subscribe_regex(new_sub);
    }

    template <typename Data, int scheme>
    void _unsubscribe(
        const goby::middleware::Group& group,
        const middleware::Subscriber<Data>& /*subscriber*/ = middleware::Subscriber<Data>())
    {
        std::string identifier =
            _make_identifier<Data, scheme>(group, IdentifierWildcard::PROCESS_THREAD_WILDCARD);

        portal_subscriptions_.erase(identifier);

        // If no forwarded subscriptions, do the actual unsubscribe
        if (forwarder_subscriptions_.count(identifier) == 0)
            zmq_main_.unsubscribe(identifier);
    }

    void _unsubscribe_all(
        const std::string& subscriber_id = identifier_part_to_string(std::this_thread::get_id()))
    {
        // portal unsubscribe
        if (subscriber_id == identifier_part_to_string(std::this_thread::get_id()))
        {
            for (const auto& p : portal_subscriptions_)
            {
                const auto& identifier = p.first;
                if (forwarder_subscriptions_.count(identifier) == 0)
                    zmq_main_.unsubscribe(identifier);
            }
            portal_subscriptions_.clear();
        }
        else // forwarder unsubscribe
        {
            while (forwarder_subscription_identifiers_[subscriber_id].size() > 0)
                _forwarder_unsubscribe(
                    subscriber_id,
                    forwarder_subscription_identifiers_[subscriber_id].begin()->first);
        }

        // regex
        if (regex_subscriptions_.size() > 0)
        {
            regex_subscriptions_.erase(subscriber_id);
            if (regex_subscriptions_.empty())
                zmq_main_.unsubscribe("/");
        }
    }

    int _poll(std::unique_ptr<std::unique_lock<std::timed_mutex>>& lock)
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
                    if (lock)
                        lock.reset();

                    const auto& data = control_msg.received_data();

                    std::string group, type, thread;
                    int scheme, process;
                    std::tie(group, scheme, type, process, thread) = parse_identifier(data);
                    std::string identifier = _make_identifier(
                        type, scheme, group, IdentifierWildcard::PROCESS_THREAD_WILDCARD);

                    // build a set so if any of the handlers unsubscribes, we still have a pointer to the middleware::SerializationHandlerBase<>
                    std::vector<std::weak_ptr<const middleware::SerializationHandlerBase<>>>
                        subs_to_post;
                    auto portal_range = portal_subscriptions_.equal_range(identifier);
                    for (auto it = portal_range.first; it != portal_range.second; ++it)
                        subs_to_post.push_back(it->second);
                    auto forwarder_it = forwarder_subscriptions_.find(identifier);
                    if (forwarder_it != forwarder_subscriptions_.end())
                        subs_to_post.push_back(forwarder_it->second);

                    // actually post the data
                    {
                        const auto& data = control_msg.received_data();
                        auto null_delim_it = std::find(std::begin(data), std::end(data), '\0');
                        for (auto& sub : subs_to_post)
                        {
                            if (auto sub_sp = sub.lock())
                                sub_sp->post(null_delim_it + 1, data.end());
                        }
                    }

                    if (!regex_subscriptions_.empty())
                    {
                        auto null_delim_it = std::find(std::begin(data), std::end(data), '\0');

                        bool forwarder_subscription_posted = false;
                        for (auto& sub : regex_subscriptions_)
                        {
                            // only post at most once for forwarders as the threads will filter
                            bool is_forwarded_sub =
                                sub.first != identifier_part_to_string(std::this_thread::get_id());
                            if (is_forwarded_sub && forwarder_subscription_posted)
                                continue;

                            if (sub.second->post(null_delim_it + 1, data.end(), scheme, type,
                                                 group) &&
                                is_forwarded_sub)
                                forwarder_subscription_posted = true;
                        }
                    }
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

                    _publish<protobuf::ManagerRequest, middleware::MarshallingScheme::PROTOBUF>(
                        req, groups::manager_request,
                        middleware::Publisher<protobuf::ManagerRequest>(), true);
                }
                break;

                default: break;
            }
            zmq_main_.control_buffer().pop_front();
        }
        return items;
    }

    void _receive_publication_forwarded(
        const goby::middleware::protobuf::SerializerTransporterMessage& msg)
    {
        std::string identifier =
            _make_identifier(msg.key().type(), msg.key().marshalling_scheme(), msg.key().group(),
                             IdentifierWildcard::NO_WILDCARDS) +
            '\0';
        auto& bytes = msg.data();
        zmq_main_.publish(identifier, &bytes[0], bytes.size());
    }

    void _receive_subscription_forwarded(
        const std::shared_ptr<const middleware::SerializationHandlerBase<>>& subscription)
    {
        std::string identifier = _make_identifier(subscription->type_name(), subscription->scheme(),
                                                  subscription->subscribed_group(),
                                                  IdentifierWildcard::PROCESS_THREAD_WILDCARD);

        switch (subscription->action())
        {
            case middleware::SerializationHandlerBase<>::SubscriptionAction::SUBSCRIBE:
            {
                // insert if this thread hasn't already subscribed
                if (forwarder_subscription_identifiers_[subscription->subscriber_id()].count(
                        identifier) == 0)
                {
                    // first to subscribe from a Forwarder
                    if (forwarder_subscriptions_.count(identifier) == 0)
                    {
                        // first to subscribe (locally or forwarded)
                        if (portal_subscriptions_.count(identifier) == 0)
                            zmq_main_.subscribe(identifier);

                        // create Forwarder subscription
                        forwarder_subscriptions_.insert(std::make_pair(identifier, subscription));
                    }
                    forwarder_subscription_identifiers_[subscription->subscriber_id()].insert(
                        std::make_pair(identifier, forwarder_subscriptions_.find(identifier)));
                }
            }
            break;

            case middleware::SerializationHandlerBase<>::SubscriptionAction::UNSUBSCRIBE:
            {
                _forwarder_unsubscribe(subscription->subscriber_id(), identifier);
            }
            break;

            default: break;
        }
    }

    void _forwarder_unsubscribe(const std::string& subscriber_id, const std::string& identifier)
    {
        auto it = forwarder_subscription_identifiers_[subscriber_id].find(identifier);
        if (it != forwarder_subscription_identifiers_[subscriber_id].end())
        {
            bool no_forwarder_subscribers = true;
            for (const auto& p : forwarder_subscription_identifiers_)
            {
                if (p.second.count(identifier) != 0)
                {
                    no_forwarder_subscribers = false;
                    break;
                }
            }

            // if no Forwarder subscriptions left
            if (no_forwarder_subscribers)
            {
                // erase the Forwarder subscription
                forwarder_subscriptions_.erase(it->second);

                // do the actual unsubscribe if we aren't subscribe locally as well
                if (portal_subscriptions_.count(identifier) == 0)
                    zmq_main_.unsubscribe(identifier);
            }

            forwarder_subscription_identifiers_[subscriber_id].erase(it);
        }
    }

    void _receive_regex_subscription_forwarded(
        std::shared_ptr<const middleware::SerializationSubscriptionRegex> subscription)
    {
        _subscribe_regex(subscription);
    }

    void _subscribe_regex(
        const std::shared_ptr<const middleware::SerializationSubscriptionRegex>& new_sub)
    {
        if (regex_subscriptions_.empty())
            zmq_main_.subscribe("/");

        regex_subscriptions_.insert(std::make_pair(new_sub->subscriber_id(), new_sub));
    }

    template <typename Data, int scheme>
    std::string _make_identifier(const goby::middleware::Group& group, IdentifierWildcard wildcard)
    {
        return _make_identifier(middleware::SerializerParserHelper<Data, scheme>::type_name(),
                                scheme, group, wildcard);
    }

    template <typename Data, int scheme>
    std::string _make_fully_qualified_identifier(const Data& d,
                                                 const goby::middleware::Group& group)
    {
        return _make_identifier<Data, scheme>(d, group, IdentifierWildcard::THREAD_WILDCARD) +
               id_component(std::this_thread::get_id(), threads_);
    }

    template <typename Data, int scheme>
    std::string _make_identifier(const Data& d, const goby::middleware::Group& group,
                                 IdentifierWildcard wildcard)
    {
        return _make_identifier(middleware::SerializerParserHelper<Data, scheme>::type_name(d),
                                scheme, group, wildcard);
    }

    std::string _make_identifier(const std::string& type_name, int scheme, const std::string& group,
                                 IdentifierWildcard wildcard)
    {
        return make_identifier(type_name, scheme, group, wildcard, process_, &schemes_, &threads_);
    }

    // group, scheme, type, process, thread
    std::tuple<std::string, int, std::string, int, std::size_t>
    parse_identifier(const std::string& identifier)
    {
        const int number_elements = 5;
        std::string::size_type previous_slash = 0;
        std::vector<std::string> elem;
        for (auto i = 0; i < number_elements; ++i)
        {
            auto slash_pos = identifier.find('/', previous_slash + 1);
            elem.push_back(identifier.substr(previous_slash + 1, slash_pos - (previous_slash + 1)));
            previous_slash = slash_pos;
        }
        return std::make_tuple(elem[0], middleware::MarshallingScheme::from_string(elem[1]),
                               elem[2], std::stoi(elem[3]), std::stoull(elem[4], nullptr, 16));
    }

  private:
    const protobuf::InterProcessPortalConfig cfg_;

    std::unique_ptr<std::thread> zmq_thread_;
    std::atomic<bool> zmq_alive_{true};
    zmq::context_t zmq_context_;
    InterProcessPortalMainThread zmq_main_;
    InterProcessPortalReadThread zmq_read_thread_;

    // maps identifier to subscription
    std::unordered_multimap<std::string,
                            std::shared_ptr<const middleware::SerializationHandlerBase<>>>
        portal_subscriptions_;
    // only one subscription for each forwarded identifier
    std::unordered_map<std::string, std::shared_ptr<const middleware::SerializationHandlerBase<>>>
        forwarder_subscriptions_;
    std::unordered_map<
        std::string, std::unordered_map<std::string, typename decltype(
                                                         forwarder_subscriptions_)::const_iterator>>
        forwarder_subscription_identifiers_;

    std::unordered_multimap<std::string,
                            std::shared_ptr<const middleware::SerializationSubscriptionRegex>>
        regex_subscriptions_;
    std::string process_{std::to_string(getpid())};
    std::unordered_map<int, std::string> schemes_;
    std::unordered_map<std::thread::id, std::string> threads_;

    bool ready_{false};
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

    Manager(zmq::context_t& context, const protobuf::InterProcessPortalConfig& cfg,
            const Router& router, const protobuf::InterProcessManagerHold& hold)
        : Manager(context, cfg, router)
    {
        for (const auto& req_c : hold.required_client()) required_clients_.insert(req_c);
    }

    void run();
    bool hold_state();

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

    std::string zmq_filter_req_{make_identifier(
        middleware::SerializerParserHelper<
            protobuf::ManagerRequest, middleware::scheme<protobuf::ManagerRequest>()>::type_name(),
        middleware::scheme<protobuf::ManagerRequest>(), groups::manager_request,
        IdentifierWildcard::PROCESS_THREAD_WILDCARD, std::to_string(getpid()))};

    std::string zmq_filter_rep_{
        make_identifier(middleware::SerializerParserHelper<
                            protobuf::ManagerResponse,
                            middleware::scheme<protobuf::ManagerResponse>()>::type_name(),
                        middleware::scheme<protobuf::ManagerResponse>(), groups::manager_response,
                        IdentifierWildcard::NO_WILDCARDS, std::to_string(getpid())) +
        std::string(1, '\0')};
}; // namespace zeromq

template <typename InnerTransporter = middleware::NullTransporter>
using InterProcessPortal =
    InterProcessPortalImplementation<InnerTransporter, middleware::InterProcessPortalBase>;

} // namespace zeromq
} // namespace goby

#endif
