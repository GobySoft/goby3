// Copyright 2016-2020:
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

#ifndef TransportInterProcessZeroMQ20170807H
#define TransportInterProcessZeroMQ20170807H

#include <tuple>
#include <zmq.hpp>

#include "goby/middleware/common.h"
#include "goby/middleware/transport/interprocess.h"
#include "goby/zeromq/protobuf/interprocess_config.pb.h"
#include "goby/zeromq/protobuf/interprocess_zeromq.pb.h"

#if ZMQ_VERSION <= ZMQ_MAKE_VERSION(4, 3, 1)
#define USE_OLD_ZMQ_CPP_API
#endif

namespace goby
{
namespace zeromq
{
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
    bool ready() { return publish_socket_configured_; }

    bool recv(protobuf::InprocControl* control_msg,
              zmq_recv_flags_type flags = zmq_recv_flags_type());
    void set_publish_cfg(const protobuf::Socket& cfg);
    void publish(const std::string& identifier, const char* bytes, int size);
    void subscribe(const std::string& identifier);
    void unsubscribe(const std::string& identifier);
    void reader_shutdown();

  private:
    void send_control_msg(const protobuf::InprocControl& control);

  private:
    zmq::socket_t control_socket_;
    zmq::socket_t publish_socket_;
    bool publish_socket_configured_{false};
    std::deque<std::pair<std::string, std::vector<char>>>
        publish_queue_; //used before publish_socket_configured_ == true
};

// run in a separate thread to allow zmq_.poll() to block without interrupting the main thread
class InterProcessPortalReadThread
{
  public:
    InterProcessPortalReadThread(const protobuf::InterProcessPortalConfig& cfg,
                                 zmq::context_t& context, std::atomic<bool>& alive,
                                 std::shared_ptr<std::condition_variable_any> poller_cv);
    void run();

  private:
    void poll(long timeout_ms = -1);
    void control_data(const zmq::message_t& zmq_msg);
    void subscribe_data(const zmq::message_t& zmq_msg);
    void manager_data(const zmq::message_t& zmq_msg);
    void send_control_msg(const protobuf::InprocControl& control);

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

    friend Base;
    friend typename Base::Base;

  private:
    void _init()
    {
        goby::glog.set_lock_action(goby::util::logger_lock::lock);

        // start zmq read thread
        zmq_thread_.reset(new std::thread([this]() { zmq_read_thread_.run(); }));

        while (!zmq_main_.ready())
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
    }

    template <typename Data, int scheme>
    void _publish(const Data& d, const goby::middleware::Group& group,
                  const middleware::Publisher<Data>& publisher)
    {
        std::vector<char> bytes(middleware::SerializerParserHelper<Data, scheme>::serialize(d));
        std::string identifier = _make_fully_qualified_identifier<Data, scheme>(d, group) + '\0';
        zmq_main_.publish(identifier, &bytes[0], bytes.size());
    }

    template <typename Data, int scheme>
    void _subscribe(std::function<void(std::shared_ptr<const Data> d)> f,
                    const goby::middleware::Group& group,
                    const middleware::Subscriber<Data>& subscriber)
    {
        std::string identifier =
            _make_identifier<Data, scheme>(group, IdentifierWildcard::PROCESS_THREAD_WILDCARD);

        auto subscription = std::make_shared<middleware::SerializationSubscription<Data, scheme>>(
            f, group,
            middleware::Subscriber<Data>(goby::middleware::protobuf::TransporterConfig(),
                                         [=](const Data& d) { return group; }));

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
    void
    _unsubscribe(const goby::middleware::Group& group,
                 const middleware::Subscriber<Data>& subscriber = middleware::Subscriber<Data>())
    {
        std::string identifier =
            _make_identifier<Data, scheme>(group, IdentifierWildcard::PROCESS_THREAD_WILDCARD);

        portal_subscriptions_.erase(identifier);

        // If no forwarded subscriptions, do the actual unsubscribe
        if (forwarder_subscriptions_.count(identifier) == 0)
            zmq_main_.unsubscribe(identifier);
    }

    void _unsubscribe_all(const std::string subscriber_id = to_string(std::this_thread::get_id()))
    {
        // portal unsubscribe
        if (subscriber_id == to_string(std::this_thread::get_id()))
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
        protobuf::InprocControl control_msg;

#ifdef USE_OLD_ZMQ_CPP_API
        int flags = ZMQ_NOBLOCK;
#else
        auto flags = zmq::recv_flags::dontwait;
#endif

        while (zmq_main_.recv(&control_msg, flags))
        {
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
                                sub.first != to_string(std::this_thread::get_id());
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

                default: break;
            }
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
        std::shared_ptr<const middleware::SerializationHandlerBase<>> subscription)
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

    void _forwarder_unsubscribe(std::string subscriber_id, std::string identifier)
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

    void _subscribe_regex(std::shared_ptr<const middleware::SerializationSubscriptionRegex> new_sub)
    {
        if (regex_subscriptions_.empty())
            zmq_main_.subscribe("/");

        regex_subscriptions_.insert(std::make_pair(new_sub->subscriber_id(), new_sub));
    }

    enum class IdentifierWildcard
    {
        NO_WILDCARDS,
        THREAD_WILDCARD,
        PROCESS_THREAD_WILDCARD
    };

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
        switch (wildcard)
        {
            default:
            case IdentifierWildcard::NO_WILDCARDS:
                return ("/" + group + "/" + id_component(scheme, schemes_) + type_name + "/" +
                        process_ + "/" + id_component(std::this_thread::get_id(), threads_));
            case IdentifierWildcard::THREAD_WILDCARD:
                return ("/" + group + "/" + id_component(scheme, schemes_) + type_name + "/" +
                        process_ + "/");
            case IdentifierWildcard::PROCESS_THREAD_WILDCARD:
                return ("/" + group + "/" + id_component(scheme, schemes_) + type_name + "/");
        }
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
                               elem[2], std::stoi(elem[3]), std::stoull(elem[4], 0, 16));
    }

    template <typename Key>
    const std::string& id_component(const Key& k, std::unordered_map<Key, std::string>& map)
    {
        auto it = map.find(k);
        if (it != map.end())
            return it->second;

        std::string v = to_string(k) + "/";
        auto it_pair = map.insert(std::make_pair(k, v));
        return it_pair.first->second;
    }

    static std::string to_string(std::thread::id i) { return goby::middleware::thread_id(i); }

    // used by scheme
    static std::string to_string(int i) { return middleware::MarshallingScheme::to_string(i); }

  private:
    const protobuf::InterProcessPortalConfig& cfg_;

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
            const Router& router)
        : context_(context), cfg_(cfg), router_(router)
    {
    }

    void run();

  private:
    zmq::context_t& context_;
    const protobuf::InterProcessPortalConfig& cfg_;
    const Router& router_;
};

template <typename InnerTransporter = middleware::NullTransporter>
using InterProcessPortal =
    InterProcessPortalImplementation<InnerTransporter, middleware::InterProcessPortalBase>;

} // namespace zeromq
} // namespace goby

#endif
