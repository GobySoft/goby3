// Copyright 2019:
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

#ifndef TransportInterThread20160609H
#define TransportInterThread20160609H

#include "goby/middleware/group.h"
#include "goby/middleware/transport/detail/subscription_store.h"
#include "goby/middleware/transport/null.h"
#include "goby/middleware/transport/poller.h"

namespace goby
{
namespace middleware
{
/// \brief A transporter for the interthread layer
///
/// As no layer exists inside the interthread layer, no distinction is made between interthread "portals" and "forwarders". This class serves both purposes, providing a no-copy publish/subscribe interface for interthread communications using std::shared_ptr (for maximum efficiency, use the shared pointer overloads for publish). As no copy is made, the publisher must not modify the underlying data after calling publish, as this would lead to potentially unsafe data races when subscribed nodes read the data.
/// \code
/// InterThreadTransporter interthread;
/// auto data = std::make_shared<protobuf::NavigationReport>();
/// data->set_x(100);
/// interthread.publish<groups::nav>(data);
/// // after this point 'data' should not be mutated (but may be read or re-published)
/// \endcode
class InterThreadTransporter
    : public StaticTransporterInterface<InterThreadTransporter, NullTransporter>,
      public Poller<InterThreadTransporter>
{
  public:
    InterThreadTransporter() : data_mutex_(std::make_shared<std::mutex>()) {}

    virtual ~InterThreadTransporter()
    {
        detail::SubscriptionStoreBase::unsubscribe_all(std::this_thread::get_id());
    }

    /// \brief Scheme for interthread is always MarshallingScheme::CXX_OBJECT as the data are not serialized, but rather passed around using shared pointers
    template <typename Data> static constexpr int scheme() { return MarshallingScheme::CXX_OBJECT; }

    /// \brief Check validity of the Group for interthread use (at compile time)
    template <const Group& group> void check_validity()
    {
        static_assert((group.c_str() != nullptr) && (group.c_str()[0] != '\0'),
                      "goby::middleware::Group must have non-zero length string to publish on the "
                      "InterThread layer");
    }

    /// \brief Check validity of the Group for interthread use (for DynamicGroup at run time)
    void check_validity_runtime(const Group& group)
    {
        if ((group.c_str() == nullptr) || (group.c_str()[0] == '\0'))
            throw(goby::Exception("Group must have a non-empty string for use on InterThread"));
    }

    /// \brief Publish a message using a run-time defined DynamicGroup (const reference variant). Where possible, prefer the static variant in StaticTransporterInterface::publish()
    ///
    /// \tparam Data data type to publish. Can usually be inferred from the \c data parameter.
    /// \tparam scheme Marshalling scheme id (typically MarshallingScheme::MarshallingSchemeEnum). Can usually be inferred from the Data type.
    /// \param data Message to publish
    /// \param group group to publish this message to (typically a DynamicGroup)
    /// \param publisher Optional metadata that controls the publication or sets callbacks to monitor the result. Typically unnecessary for interprocess and inner layers.
    template <typename Data, int scheme = scheme<Data>()>
    void publish_dynamic(const Data& data, const Group& group,
                         const Publisher<Data>& publisher = Publisher<Data>())
    {
        check_validity_runtime(group);
        std::shared_ptr<Data> data_ptr(new Data(data));
        publish_dynamic<Data>(data_ptr, group, publisher);
    }

    /// \brief Publish a message using a run-time defined DynamicGroup (shared pointer to const data variant). Where possible, prefer the static variant in StaticTransporterInterface::publish()
    ///
    /// \tparam Data data type to publish. Can usually be inferred from the \c data parameter.
    /// \tparam scheme Marshalling scheme id (typically MarshallingScheme::MarshallingSchemeEnum). Can usually be inferred from the Data type.
    /// \param data Message to publish
    /// \param group group to publish this message to (typically a DynamicGroup)
    /// \param publisher Optional metadata that controls the publication or sets callbacks to monitor the result. Typically unnecessary for interprocess and inner layers.
    template <typename Data, int scheme = scheme<Data>()>
    void publish_dynamic(std::shared_ptr<const Data> data, const Group& group,
                         const Publisher<Data>& publisher = Publisher<Data>())
    {
        check_validity_runtime(group);
        detail::SubscriptionStore<Data>::publish(data, group, publisher);
    }

    /// \brief Publish a message using a run-time defined DynamicGroup (shared pointer to mutable data variant). Where possible, prefer the static variant in StaticTransporterInterface::publish()
    ///
    /// \tparam Data data type to publish. Can usually be inferred from the \c data parameter.
    /// \tparam scheme Marshalling scheme id (typically MarshallingScheme::MarshallingSchemeEnum). Can usually be inferred from the Data type.
    /// \param data Message to publish
    /// \param group group to publish this message to (typically a DynamicGroup)
    /// \param publisher Optional metadata that controls the publication or sets callbacks to monitor the result. Typically unnecessary for interprocess and inner layers.
    template <typename Data, int scheme = scheme<Data>()>
    void publish_dynamic(std::shared_ptr<Data> data, const Group& group,
                         const Publisher<Data>& publisher = Publisher<Data>())
    {
        publish_dynamic<Data, scheme>(std::shared_ptr<const Data>(data), group, publisher);
    }

    /// \brief Subscribe to a specific run-time defined group and data type (const reference variant). Where possible, prefer the static variant in StaticTransporterInterface::subscribe()
    ///
    /// \tparam Data data type to subscribe to.
    /// \tparam scheme Marshalling scheme id (typically MarshallingScheme::MarshallingSchemeEnum). Can usually be inferred from the Data type.
    /// \param f Callback function or lambda that is called upon receipt of the subscribed data
    /// \param group group to subscribe to (typically a DynamicGroup)
    /// \param subscriber Optional metadata that controls the subscription or sets callbacks to monitor the subscription result. Typically unnecessary for interprocess and inner layers.
    template <typename Data, int scheme = scheme<Data>()>
    void subscribe_dynamic(std::function<void(const Data&)> f, const Group& group,
                           const Subscriber<Data>& subscriber = Subscriber<Data>())
    {
        check_validity_runtime(group);
        detail::SubscriptionStore<Data>::subscribe([=](std::shared_ptr<const Data> pd) { f(*pd); },
                                                   group, std::this_thread::get_id(), data_mutex_,
                                                   Poller<InterThreadTransporter>::cv(),
                                                   Poller<InterThreadTransporter>::poll_mutex());
    }

    /// \brief Subscribe to a specific run-time defined group and data type (shared pointer variant). Where possible, prefer the static variant in StaticTransporterInterface::subscribe()
    ///
    /// \tparam Data data type to subscribe to.
    /// \tparam scheme Marshalling scheme id (typically MarshallingScheme::MarshallingSchemeEnum). Can usually be inferred from the Data type.
    /// \param f Callback function or lambda that is called upon receipt of the subscribed data
    /// \param group group to subscribe to (typically a DynamicGroup)
    /// \param subscriber Optional metadata that controls the subscription or sets callbacks to monitor the subscription result. Typically unnecessary for interprocess and inner layers.
    template <typename Data, int scheme = scheme<Data>()>
    void subscribe_dynamic(std::function<void(std::shared_ptr<const Data>)> f, const Group& group,
                           const Subscriber<Data>& subscriber = Subscriber<Data>())
    {
        check_validity_runtime(group);
        detail::SubscriptionStore<Data>::subscribe(
            f, group, std::this_thread::get_id(), data_mutex_, Poller<InterThreadTransporter>::cv(),
            Poller<InterThreadTransporter>::poll_mutex());
    }

    /// \brief Unsubscribe to a specific run-time defined group and data type. Where possible, prefer the static variant in StaticTransporterInterface::unsubscribe()
    ///
    /// \tparam Data data type to unsubscribe from.
    /// \tparam scheme Marshalling scheme id (typically MarshallingScheme::MarshallingSchemeEnum). Can usually be inferred from the Data type.
    /// \param group group to unsubscribe from (typically a DynamicGroup)
    template <typename Data, int scheme = scheme<Data>()>
    void unsubscribe_dynamic(const Group& group)
    {
        check_validity_runtime(group);
        detail::SubscriptionStore<Data>::unsubscribe(group, std::this_thread::get_id());
    }

    /// \brief Unsubscribe from all current subscriptions
    void unsubscribe_all()
    {
        detail::SubscriptionStoreBase::unsubscribe_all(std::this_thread::get_id());
    }

  private:
    friend Poller<InterThreadTransporter>;
    int _poll(std::unique_ptr<std::unique_lock<std::timed_mutex> >& lock)
    {
        return detail::SubscriptionStoreBase::poll_all(std::this_thread::get_id(), lock);
    }

  private:
    // protects this thread's DataQueue
    std::shared_ptr<std::mutex> data_mutex_;
};

} // namespace middleware
} // namespace goby

#endif
