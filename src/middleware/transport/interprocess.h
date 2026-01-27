// Copyright 2016-2024:
//   GobySoft, LLC (2013-)
//   Community contributors (see AUTHORS file)
// File authors:
//   Toby Schneider <toby@gobysoft.org>
//   Ryan Govostes <rgovostes+git@gmail.com>
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

#ifndef GOBY_MIDDLEWARE_TRANSPORT_INTERPROCESS_H
#define GOBY_MIDDLEWARE_TRANSPORT_INTERPROCESS_H

#include <atomic>
#include <functional>
#include <sys/types.h>
#include <thread>
#include <tuple>
#include <unistd.h>

#include "goby/middleware/group.h"

#include "goby/middleware/marshalling/interface.h"
#include "goby/middleware/transport/identifier.h"
#include "goby/middleware/transport/null.h"
#include "goby/middleware/transport/poller.h"
#include "goby/middleware/transport/serialization_handlers.h"

namespace goby
{
namespace middleware
{
/// \brief Base class for implementing transporters (both portal and forwarder) for the interprocess layer
///
/// \tparam Derived derived class (curiously recurring template pattern)
/// \tparam InnerTransporter inner layer transporter type
template <typename Derived, typename InnerTransporter>
class InterProcessTransporterBase
    : public StaticTransporterInterface<InterProcessTransporterBase<Derived, InnerTransporter>,
                                        InnerTransporter>,
      public Poller<InterProcessTransporterBase<Derived, InnerTransporter>>
{
    using InterfaceType =
        StaticTransporterInterface<InterProcessTransporterBase<Derived, InnerTransporter>,
                                   InnerTransporter>;

    using PollerType = Poller<InterProcessTransporterBase<Derived, InnerTransporter>>;

  public:
    InterProcessTransporterBase(InnerTransporter& inner)
        : InterfaceType(inner), PollerType(&this->inner())
    {
    }
    InterProcessTransporterBase() : PollerType(&this->inner()) {}

    virtual ~InterProcessTransporterBase() {}

    /// \brief returns the marshalling scheme id for a given data type on this layer
    ///
    /// \tparam Data data type
    /// \return marshalling scheme id
    template <typename Data> static constexpr int scheme()
    {
        int scheme = goby::middleware::scheme<Data>();
        // if default returns DCCL, use PROTOBUF instead
        if (scheme == MarshallingScheme::DCCL)
            scheme = MarshallingScheme::PROTOBUF;
        return scheme;
    }

    /// \brief Publish a message using a run-time defined DynamicGroup (const reference variant). Where possible, prefer the static variant in StaticTransporterInterface::publish()
    ///
    /// \tparam Data data type to publish. Can usually be inferred from the \c data parameter.
    /// \tparam scheme Marshalling scheme id (typically MarshallingScheme::MarshallingSchemeEnum). Can usually be inferred from the Data type.
    /// \param data Message to publish
    /// \param group group to publish this message to (typically a DynamicGroup)
    /// \param publisher Optional metadata that controls the publication or sets callbacks to monitor the result. Typically unnecessary for interprocess and inner layers.
    template <typename Data, int scheme = InterProcessTransporterBase::scheme<Data>()>
    void publish_dynamic(const Data& data, const Group& group,
                         const Publisher<Data>& publisher = Publisher<Data>())
    {
        check_validity_runtime(group);
        static_cast<Derived*>(this)->template _publish<Data, scheme>(data, group, publisher);
        this->inner().template publish_dynamic<Data, scheme>(data, group, publisher);
    }

    /// \brief Publish a message using a run-time defined DynamicGroup (shared pointer to const data variant). Where possible, prefer the static variant in StaticTransporterInterface::publish()
    ///
    /// \tparam Data data type to publish. Can usually be inferred from the \c data parameter.
    /// \tparam scheme Marshalling scheme id (typically MarshallingScheme::MarshallingSchemeEnum). Can usually be inferred from the Data type.
    /// \param data Message to publish
    /// \param group group to publish this message to (typically a DynamicGroup)
    /// \param publisher Optional metadata that controls the publication or sets callbacks to monitor the result. Typically unnecessary for interprocess and inner layers.
    template <typename Data, int scheme = InterProcessTransporterBase::scheme<Data>()>
    void publish_dynamic(std::shared_ptr<const Data> data, const Group& group,
                         const Publisher<Data>& publisher = Publisher<Data>())
    {
        if (data)
        {
            check_validity_runtime(group);
            static_cast<Derived*>(this)->template _publish<Data, scheme>(*data, group, publisher);
            this->inner().template publish_dynamic<Data, scheme>(data, group, publisher);
        }
    }

    /// \brief Publish a message using a run-time defined DynamicGroup (shared pointer to mutable data variant). Where possible, prefer the static variant in StaticTransporterInterface::publish()
    ///
    /// \tparam Data data type to publish. Can usually be inferred from the \c data parameter.
    /// \tparam scheme Marshalling scheme id (typically MarshallingScheme::MarshallingSchemeEnum). Can usually be inferred from the Data type.
    /// \param data Message to publish
    /// \param group group to publish this message to (typically a DynamicGroup)
    /// \param publisher Optional metadata that controls the publication or sets callbacks to monitor the result. Typically unnecessary for interprocess and inner layers.
    template <typename Data, int scheme = InterProcessTransporterBase::scheme<Data>()>
    void publish_dynamic(std::shared_ptr<Data> data, const Group& group,
                         const Publisher<Data>& publisher = Publisher<Data>())
    {
        publish_dynamic<Data, scheme>(std::shared_ptr<const Data>(data), group, publisher);
    }

    /// \brief Publish a message that has already been serialized for the given scheme
    void publish_serialized(std::string type_name, int scheme, const std::vector<char>& bytes,
                            const goby::middleware::Group& group)
    {
        check_validity_runtime(group);
        static_cast<Derived*>(this)->_publish_serialized(type_name, scheme, bytes, group);
    }

    /// \brief Subscribe to a specific run-time defined group and data type (const reference variant). Where possible, prefer the static variant in StaticTransporterInterface::subscribe()
    ///
    /// \tparam Data data type to subscribe to.
    /// \tparam scheme Marshalling scheme id (typically MarshallingScheme::MarshallingSchemeEnum). Can usually be inferred from the Data type.
    /// \param f Callback function or lambda that is called upon receipt of the subscribed data
    /// \param group group to subscribe to (typically a DynamicGroup)
    /// \param subscriber Optional metadata that controls the subscription or sets callbacks to monitor the subscription result. Typically unnecessary for interprocess and inner layers.
    template <typename Data, int scheme = InterProcessTransporterBase::scheme<Data>()>
    void subscribe_dynamic(std::function<void(const Data&)> f, const Group& group,
                           const Subscriber<Data>& subscriber = Subscriber<Data>())
    {
        check_validity_runtime(group);
        static_cast<Derived*>(this)->template _subscribe<Data, scheme>(
            [=](std::shared_ptr<const Data> d) { f(*d); }, group, subscriber);
    }

    /// \brief Subscribe to a specific run-time defined group and data type (shared pointer variant). Where possible, prefer the static variant in StaticTransporterInterface::subscribe()
    ///
    /// \tparam Data data type to subscribe to.
    /// \tparam scheme Marshalling scheme id (typically MarshallingScheme::MarshallingSchemeEnum). Can usually be inferred from the Data type.
    /// \param f Callback function or lambda that is called upon receipt of the subscribed data
    /// \param group group to subscribe to (typically a DynamicGroup)
    /// \param subscriber Optional metadata that controls the subscription or sets callbacks to monitor the subscription result. Typically unnecessary for interprocess and inner layers.
    template <typename Data, int scheme = InterProcessTransporterBase::scheme<Data>()>
    void subscribe_dynamic(std::function<void(std::shared_ptr<const Data>)> f, const Group& group,
                           const Subscriber<Data>& subscriber = Subscriber<Data>())
    {
        check_validity_runtime(group);
        static_cast<Derived*>(this)->template _subscribe<Data, scheme>(f, group, subscriber);
    }

    /// \brief Unsubscribe to a specific run-time defined group and data type. Where possible, prefer the static variant in StaticTransporterInterface::unsubscribe()
    ///
    /// \tparam Data data type to unsubscribe from.
    /// \tparam scheme Marshalling scheme id (typically MarshallingScheme::MarshallingSchemeEnum). Can usually be inferred from the Data type.
    /// \param group group to unsubscribe from (typically a DynamicGroup)
    template <typename Data, int scheme = InterProcessTransporterBase::scheme<Data>()>
    void unsubscribe_dynamic(const Group& group,
                             const Subscriber<Data>& subscriber = Subscriber<Data>())
    {
        check_validity_runtime(group);
        static_cast<Derived*>(this)->template _unsubscribe<Data, scheme>(group, subscriber);
    }

    /// \brief Unsubscribe from all current subscriptions
    void unsubscribe_all() { static_cast<Derived*>(this)->_unsubscribe_all(); }

    /// \brief Subscribe to multiple groups and/or types at once using regular expressions
    ///
    /// \param f Callback function or lambda that is called upon receipt of any messages matching the group regex and type regex
    /// \param schemes Set of marshalling schemes to match
    /// \param type_regex C++ regex to match type names (within one or more of the given schemes)
    /// \param group_regex C++ regex to match group names
    /// \return Shared pointer to SerializationSubscriptionRegex for later modification of regex parameters
    std::shared_ptr<SerializationSubscriptionRegex>
    subscribe_regex(std::function<void(const std::vector<unsigned char>&, int scheme,
                                       const std::string& type, const Group& group)>
                        f,
                    const std::set<int>& schemes, const std::string& type_regex = ".*",
                    const std::string& group_regex = ".*")
    {
        return static_cast<Derived*>(this)->_subscribe_regex(f, schemes, type_regex, group_regex);
    }

    /// \brief Subscribe to a number of types within a given group and scheme using a regular expression
    ///
    /// The marshalling scheme must implement SerializerParserHelper::parse() to use this method.
    /// \tparam Data data type to publish. Can usually be inferred from the \c data parameter.
    /// \tparam scheme Marshalling scheme id (typically MarshallingScheme::MarshallingSchemeEnum). Can usually be inferred from the Data type.
    /// \param group group to subscribe to (typically a DynamicGroup)
    /// \param f Callback function or lambda that is called upon receipt of any messages matching the given group and type regex
    /// \param type_regex C++ regex to match type names (within the given scheme)
    /// \return Shared pointer to SerializationSubscriptionRegex for later modification of regex parameters
    template <typename Data, int scheme = InterProcessTransporterBase::scheme<Data>()>
    std::shared_ptr<SerializationSubscriptionRegex> subscribe_type_regex(
        std::function<void(std::shared_ptr<const Data>, const std::string& type)> f,
        const Group& group, const std::string& type_regex = ".*")
    {
        std::regex special_chars{R"([-[\]{}()*+?.,\^$|#\s])"};
        std::string sanitized_group =
            std::regex_replace(std::string(group), special_chars, R"(\$&)");

        auto regex_lambda = [=](const std::vector<unsigned char>& data, int schm,
                                const std::string& type, const Group& grp)
        {
            auto data_begin = data.begin(), data_end = data.end(), actual_end = data.end();
            auto msg =
                SerializerParserHelper<Data, scheme>::parse(data_begin, data_end, actual_end, type);
            f(msg, type);
        };

        return static_cast<Derived*>(this)->_subscribe_regex(regex_lambda, {scheme}, type_regex,
                                                             "^" + sanitized_group + "$");
    }

    /// \brief Subscribe to a number of types within a given group and scheme using a regular expression
    ///
    /// The marshalling scheme must implement SerializerParserHelper::parse() to use this method.
    /// \tparam group group to publish this message to (reference to constexpr Group)
    /// \tparam Data data type to publish. Can usually be inferred from the \c data parameter.
    /// \tparam scheme Marshalling scheme id (typically MarshallingScheme::MarshallingSchemeEnum). Can usually be inferred from the Data type.
    /// \param f Callback function or lambda that is called upon receipt of any messages matching the given group and type regex
    /// \param type_regex C++ regex to match type names (within the given scheme)
    template <const Group& group, typename Data,
              int scheme = InterProcessTransporterBase::scheme<Data>()>
    void subscribe_type_regex(
        std::function<void(std::shared_ptr<const Data>, const std::string& type)> f,
        const std::string& type_regex = ".*")
    {
        subscribe_type_regex(f, group, type_regex);
    }

    /// \brief Check validity of the Group for interthread use (at compile time)
    ///
    /// This layer requires a valid string group
    template <const Group& group> void check_validity()
    {
        static_assert((group.c_str() != nullptr) && (group.c_str()[0] != '\0'),
                      "goby::middleware::Group must have non-zero length string to publish on the "
                      "InterProcess layer");
    }

    /// \brief Check validity of the Group for interthread use (for DynamicGroup at run time)
    void check_validity_runtime(const Group& group)
    {
        if ((group.c_str() == nullptr) || (group.c_str()[0] == '\0'))
            throw(goby::Exception("Group must have a non-empty string for use on InterProcess"));
    }

  protected:
    static constexpr Group to_portal_group_{"goby::middleware::interprocess::to_portal"};
    static constexpr Group regex_group_{"goby::middleware::interprocess::regex"};
    static constexpr Group from_portal_group_{"goby::middleware::interprocess::from_portal"};

  private:
    friend PollerType;
    int _poll(std::unique_ptr<std::unique_lock<std::timed_mutex>>& lock)
    {
        return static_cast<Derived*>(this)->_poll(lock);
    }
};

template <typename Derived, typename InnerTransporter>
constexpr goby::middleware::Group
    InterProcessTransporterBase<Derived, InnerTransporter>::to_portal_group_;
template <typename Derived, typename InnerTransporter>
constexpr goby::middleware::Group
    InterProcessTransporterBase<Derived, InnerTransporter>::regex_group_;
template <typename Derived, typename InnerTransporter>
constexpr goby::middleware::Group
    InterProcessTransporterBase<Derived, InnerTransporter>::from_portal_group_;

/// \brief Implements the forwarder concept for the interprocess layer
///
/// The forwarder is intended to be used by inner nodes within the layer that do not connect directly to other nodes on that layer. For example, the main thread might instantiate a portal and then spawn several threads that instantiate forwarders. These auxiliary threads can then communicate on the interprocess layer as if they had a direct connection to other interprocess nodes.
/// \tparam InnerTransporter The type of the inner transporter used to forward data to and from this node
template <typename InnerTransporter>
class InterProcessForwarder
    : public InterProcessTransporterBase<InterProcessForwarder<InnerTransporter>, InnerTransporter>
{
  public:
    using Base =
        InterProcessTransporterBase<InterProcessForwarder<InnerTransporter>, InnerTransporter>;

    /// \brief Construct a forwarder for the interprocess layer
    ///
    /// \param inner A reference to the inner transporter used to forward messages to and from the portal
    InterProcessForwarder(InnerTransporter& inner) : Base(inner)
    {
        this->inner()
            .template subscribe<Base::regex_group_,
                                goby::middleware::protobuf::SerializerTransporterMessage>(
                [this](
                    std::shared_ptr<const goby::middleware::protobuf::SerializerTransporterMessage>
                        msg) { _receive_regex_data_forwarded(msg); });
    }
    virtual ~InterProcessForwarder()
    {
        this->unsubscribe_all();

        // TODO - remove by adding in an explicit handshake with the unsubscribe_all publication so that we don't delete ourself (and thus our inner()) before the Portal has deleted all the subscriptions
        usleep(1e5);
    }

    friend Base;

  private:
    template <typename Data, int scheme>
    void _publish(const Data& d, const Group& group, const Publisher<Data>& publisher)
    {
        // create and forward publication to edge
        std::vector<char> bytes(SerializerParserHelper<Data, scheme>::serialize(d));
        std::string* sbytes = new std::string(bytes.begin(), bytes.end());
        auto msg = std::make_shared<goby::middleware::protobuf::SerializerTransporterMessage>();
        auto* key = msg->mutable_key();

        key->set_marshalling_scheme(scheme);
        key->set_type(SerializerParserHelper<Data, scheme>::type_name(d));
        key->set_group(std::string(group));
        msg->set_allocated_data(sbytes);

        *key->mutable_cfg() = publisher.cfg();

        this->inner().template publish<Base::to_portal_group_>(msg);
    }

    template <typename Data, int scheme>
    void _subscribe(std::function<void(std::shared_ptr<const Data> d)> f, const Group& group,
                    const Subscriber<Data>& subscriber)
    {
        this->inner().template subscribe_dynamic<Data, scheme>(f, group);

        // forward subscription to edge
        auto inner_publication_lambda = [=](std::shared_ptr<const Data> d)
        { this->inner().template publish_dynamic<Data, scheme>(d, group); };

        auto subscription = std::make_shared<SerializationSubscription<Data, scheme>>(
            inner_publication_lambda, group,
            middleware::Subscriber<Data>(goby::middleware::protobuf::TransporterConfig(),
                                         [=](const Data& d) { return group; }));

        this->inner().template publish<Base::to_portal_group_, SerializationHandlerBase<>>(
            subscription);
    }

    template <typename Data, int scheme>
    void _unsubscribe(const Group& group, const Subscriber<Data>& subscriber)
    {
        this->inner().template unsubscribe_dynamic<Data, scheme>(group, subscriber);

        auto unsubscription = std::shared_ptr<SerializationHandlerBase<>>(
            new SerializationUnSubscription<Data, scheme>(group));

        this->inner().template publish<Base::to_portal_group_, SerializationHandlerBase<>>(
            unsubscription);
    }

    void _publish_serialized(std::string type_name, int scheme, const std::vector<char>& bytes,
                             const goby::middleware::Group& group)
    {
        auto msg = std::make_shared<goby::middleware::protobuf::SerializerTransporterMessage>();
        auto* key = msg->mutable_key();

        key->set_marshalling_scheme(scheme);
        key->set_type(type_name);
        key->set_group(std::string(group));
        msg->set_data(std::string(bytes.begin(), bytes.end()));

        this->inner().template publish<Base::to_portal_group_>(msg);
    }

    void _unsubscribe_all()
    {
        regex_subscriptions_.clear();
        auto all = std::make_shared<SerializationUnSubscribeAll>();
        this->inner().template publish<Base::to_portal_group_, SerializationUnSubscribeAll>(all);
    }

    std::shared_ptr<SerializationSubscriptionRegex>
    _subscribe_regex(std::function<void(const std::vector<unsigned char>&, int scheme,
                                        const std::string& type, const Group& group)>
                         f,
                     const std::set<int>& schemes, const std::string& type_regex = ".*",
                     const std::string& group_regex = ".*")
    {
        auto inner_publication_lambda = [=](const std::vector<unsigned char>& data, int scheme,
                                            const std::string& type, const Group& group)
        {
            std::shared_ptr<goby::middleware::protobuf::SerializerTransporterMessage>
                forwarded_data(new goby::middleware::protobuf::SerializerTransporterMessage);
            forwarded_data->mutable_key()->set_marshalling_scheme(scheme);
            forwarded_data->mutable_key()->set_type(type);
            forwarded_data->mutable_key()->set_group(group);
            forwarded_data->set_data(std::string(data.begin(), data.end()));
            this->inner().template publish<Base::regex_group_>(forwarded_data);
        };

        auto portal_subscription = std::make_shared<SerializationSubscriptionRegex>(
            inner_publication_lambda, schemes, type_regex, group_regex);
        this->inner().template publish<Base::to_portal_group_, SerializationSubscriptionRegex>(
            portal_subscription);

        auto local_subscription = std::shared_ptr<SerializationSubscriptionRegex>(
            new SerializationSubscriptionRegex(f, schemes, type_regex, group_regex));
        regex_subscriptions_.insert(local_subscription);
        return local_subscription;
    }

    void _receive_regex_data_forwarded(
        std::shared_ptr<const goby::middleware::protobuf::SerializerTransporterMessage> msg)
    {
        const auto& bytes = msg->data();
        for (auto& sub : regex_subscriptions_)
            sub->post(bytes.begin(), bytes.end(), msg->key().marshalling_scheme(),
                      msg->key().type(), msg->key().group());
    }

    int _poll(std::unique_ptr<std::unique_lock<std::timed_mutex>>& lock)
    {
        return 0;
    } // A forwarder is a shell, only the inner Transporter has data

  private:
    std::set<std::shared_ptr<const SerializationSubscriptionRegex>> regex_subscriptions_;
};

template <typename Derived, typename InnerTransporter>
class InterProcessPortalBase : public InterProcessTransporterBase<Derived, InnerTransporter>,
                               public InterProcessIdentifierManager
{
  public:
    using Base = InterProcessTransporterBase<Derived, InnerTransporter>;

    InterProcessPortalBase(InnerTransporter& inner) : Base(inner) { _init(); }
    InterProcessPortalBase() { _init(); }

    virtual ~InterProcessPortalBase() {}

    friend Base;

  protected:
    template <typename Data, int scheme>
    void _publish(const Data& d, const goby::middleware::Group& group,
                  const middleware::Publisher<Data>& /*publisher*/)
    {
        std::vector<char> bytes(middleware::SerializerParserHelper<Data, scheme>::serialize(d));
        std::string type_name = middleware::SerializerParserHelper<Data, scheme>::type_name(d);
        _publish_serialized(type_name, scheme, bytes, group);
    }

    std::shared_ptr<middleware::SerializationSubscriptionRegex> _subscribe_regex(
        std::function<void(const std::vector<unsigned char>&, int scheme, const std::string& type,
                           const goby::middleware::Group& group)>
            f,
        const std::set<int>& schemes, const std::string& type_regex, const std::string& group_regex)
    {
        auto new_sub = std::make_shared<middleware::SerializationSubscriptionRegex>(
            f, schemes, type_regex, group_regex);
        static_cast<Derived*>(this)->_subscribe_regex_serialized(new_sub);
        return new_sub;
    }

    template <typename Data, int scheme>
    void _subscribe(std::function<void(std::shared_ptr<const Data> d)> f,
                    const goby::middleware::Group& group,
                    const middleware::Subscriber<Data>& /*subscriber*/)
    {
        std::string identifier = this->template _make_identifier<Data, scheme>(
            group, IdentifierWildcard::PROCESS_THREAD_WILDCARD);

        auto subscription = std::make_shared<middleware::SerializationSubscription<Data, scheme>>(
            f, group,
            middleware::Subscriber<Data>(goby::middleware::protobuf::TransporterConfig(),
                                         [=](const Data& /*d*/) { return group; }));

        if (forwarder_subscriptions_.count(identifier) == 0 &&
            portal_subscriptions_.count(identifier) == 0)
            static_cast<Derived*>(this)->_do_portal_subscribe(identifier);

        portal_subscriptions_.insert(std::make_pair(identifier, subscription));
    }

    template <typename Data, int scheme>
    void _unsubscribe(
        const goby::middleware::Group& group,
        const middleware::Subscriber<Data>& /*subscriber*/ = middleware::Subscriber<Data>())
    {
        std::string identifier = this->template _make_identifier<Data, scheme>(
            group, IdentifierWildcard::PROCESS_THREAD_WILDCARD);

        portal_subscriptions_.erase(identifier);

        // If no forwarded subscriptions, do the actual unsubscribe
        if (forwarder_subscriptions_.count(identifier) == 0)
            static_cast<Derived*>(this)->_do_portal_unsubscribe(identifier);
    }
    void _handle_received_data(std::unique_ptr<std::unique_lock<std::timed_mutex>>& lock,
                               const std::string& data)
    {
        if (lock)
            lock.reset();

        std::string group, type;
        int scheme, process;
        std::size_t thread;
        std::tie(group, scheme, type, process, thread) = this->parse_identifier(data);
        std::string identifier = this->_make_identifier(
            type, scheme, group, IdentifierWildcard::PROCESS_THREAD_WILDCARD);

        // build a set so if any of the handlers unsubscribes, we still have a pointer to the middleware::SerializationHandlerBase<>
        std::vector<std::weak_ptr<const middleware::SerializationHandlerBase<>>> subs_to_post;
        auto portal_range = portal_subscriptions_.equal_range(identifier);
        for (auto it = portal_range.first; it != portal_range.second; ++it)
            subs_to_post.push_back(it->second);
        auto forwarder_it = forwarder_subscriptions_.find(identifier);
        if (forwarder_it != forwarder_subscriptions_.end())
            subs_to_post.push_back(forwarder_it->second);

        // actually post the data
        {
            auto null_delim_it = std::find(std::begin(data), std::end(data),
                                           InterProcessIdentifierManager::end_delimiter);
            for (auto& sub : subs_to_post)
            {
                if (auto sub_sp = sub.lock())
                    sub_sp->post(null_delim_it + 1, data.end());
            }
        }

        if (!regex_subscriptions_.empty())
        {
            auto null_delim_it = std::find(std::begin(data), std::end(data),
                                           InterProcessIdentifierManager::end_delimiter);

            bool forwarder_subscription_posted = false;
            for (auto& sub : regex_subscriptions_)
            {
                // only post at most once for forwarders as the threads will filter
                bool is_forwarded_sub =
                    sub.first != middleware::identifier_part_to_string(std::this_thread::get_id());
                if (is_forwarded_sub && forwarder_subscription_posted)
                    continue;

                if (sub.second->post(null_delim_it + 1, data.end(), scheme, type, group) &&
                    is_forwarded_sub)
                    forwarder_subscription_posted = true;
            }
        }
    }

  private:
    void _init()
    {
        using goby::middleware::protobuf::SerializerTransporterMessage;
        this->inner().template subscribe<Base::to_portal_group_, SerializerTransporterMessage>(
            [this](std::shared_ptr<const SerializerTransporterMessage> d)
            {
                std::vector<char> data(d->data().begin(), d->data().end());
                _publish_serialized(d->key().type(), d->key().marshalling_scheme(), data,
                                    goby::middleware::DynamicGroup(d->key().group()));
            });

        this->inner().template subscribe<Base::to_portal_group_, SerializationHandlerBase<>>(
            [this](std::shared_ptr<const middleware::SerializationHandlerBase<>> s)
            { _receive_subscription_forwarded(s); });

        this->inner().template subscribe<Base::to_portal_group_, SerializationSubscriptionRegex>(
            [this](std::shared_ptr<const middleware::SerializationSubscriptionRegex> s)
            { _subscribe_regex_serialized(s); });

        this->inner().template subscribe<Base::to_portal_group_, SerializationUnSubscribeAll>(
            [this](std::shared_ptr<const middleware::SerializationUnSubscribeAll> s)
            { _unsubscribe_all(s->subscriber_id()); });
    }

    void _unsubscribe_all(const std::string& subscriber_id =
                              middleware::identifier_part_to_string(std::this_thread::get_id()))
    {
        // portal unsubscribe
        if (subscriber_id == middleware::identifier_part_to_string(std::this_thread::get_id()))
        {
            for (const auto& p : portal_subscriptions_)
            {
                const auto& identifier = p.first;
                if (forwarder_subscriptions_.count(identifier) == 0)
                    static_cast<Derived*>(this)->_do_portal_unsubscribe(identifier);
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
                static_cast<Derived*>(this)->_do_portal_wildcard_unsubscribe();
        }
    }

    void _receive_subscription_forwarded(
        const std::shared_ptr<const middleware::SerializationHandlerBase<>>& subscription)
    {
        std::string identifier = this->_make_identifier(
            subscription->type_name(), subscription->scheme(), subscription->subscribed_group(),
            IdentifierWildcard::PROCESS_THREAD_WILDCARD);

        goby::glog.is_debug2() &&
            goby::glog << "Received subscription forwarded for identifier [" << identifier
                       << "] from subscriber id: " << subscription->subscriber_id() << std::endl;

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
                            static_cast<Derived*>(this)->_do_portal_subscribe(identifier);

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
                    static_cast<Derived*>(this)->_do_portal_unsubscribe(identifier);
            }

            forwarder_subscription_identifiers_[subscriber_id].erase(it);
        }
    }

    void _subscribe_regex_serialized(
        const std::shared_ptr<const middleware::SerializationSubscriptionRegex>& new_sub)
    {
        if (regex_subscriptions_.empty())
            static_cast<Derived*>(this)->_do_portal_wildcard_subscribe();

        regex_subscriptions_.insert(std::make_pair(new_sub->subscriber_id(), new_sub));
    }

    void _publish_serialized(std::string type_name, int scheme, const std::vector<char>& bytes,
                             const goby::middleware::Group& group)
    {
        std::string identifier =
            this->_make_identifier(type_name, scheme, group, IdentifierWildcard::NO_WILDCARDS) +
            InterProcessIdentifierManager::end_delimiter;
        static_cast<Derived*>(this)->_do_publish(identifier, bytes);
    }

  private:
    // portal_subscriptions_ and forwarder_subscriptions_: maps identifier to subscription
    std::unordered_multimap<std::string,
                            std::shared_ptr<const middleware::SerializationHandlerBase<>>>
        portal_subscriptions_;
    // only one subscription for each forwarded identifier
    std::unordered_map<std::string, std::shared_ptr<const middleware::SerializationHandlerBase<>>>
        forwarder_subscriptions_;

    // maps subscriber_id [thread id as string] to map of identifier to forwarder subscription
    std::unordered_map<
        std::string, std::unordered_map<
                         std::string, typename decltype(forwarder_subscriptions_)::const_iterator>>
        forwarder_subscription_identifiers_;

    // subscriber id to subscription
    std::unordered_multimap<std::string,
                            std::shared_ptr<const middleware::SerializationSubscriptionRegex>>
        regex_subscriptions_;
};

} // namespace middleware
} // namespace goby

#endif
