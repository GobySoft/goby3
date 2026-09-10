// Copyright 2026:
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

#ifndef GOBY_ZENOH_TRANSPORT_INTERPROCESS_H
#define GOBY_ZENOH_TRANSPORT_INTERPROCESS_H

#include <deque>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <unordered_map>

#include <zenoh.hxx>

#include "goby/middleware/transport/detail/implementation_traits.h"
#include "goby/middleware/transport/detail/poller_notify.h"
#include "goby/middleware/transport/identifier.h"
#include "goby/middleware/transport/interface.h"
#include "goby/middleware/transport/interprocess.h"
#include "goby/util/debug_logger.h"

#include "goby/zenoh/protobuf/interprocess_config.pb.h"
#include "goby/zenoh/transport/detail/key_expression.h"
#include "goby/zenoh/transport/detail/tags.h"

namespace goby
{
namespace middleware
{
template <typename Data> class Publisher;
} // namespace middleware

namespace zenoh
{
/// \brief Open a Zenoh session from the Goby configuration
::zenoh::Session open_session(const protobuf::InterProcessPortalConfig& cfg);

/// \brief The key expression chunks shared by every message on one layer: "<prefix>/<platform>/<layer>"
std::string make_key_root(const protobuf::InterProcessPortalConfig& cfg, const std::string& layer);

template <typename InnerTransporter,
          template <typename Derived, typename InnerTransporterType, typename ImplementationTag_>
          class PortalBase,
          typename ImplementationTag>
class InterProcessPortalImplementation
    : public PortalBase<
          InterProcessPortalImplementation<InnerTransporter, PortalBase, ImplementationTag>,
          InnerTransporter, ImplementationTag>
{
  public:
    using Base = PortalBase<
        InterProcessPortalImplementation<InnerTransporter, PortalBase, ImplementationTag>,
        InnerTransporter, ImplementationTag>;

    InterProcessPortalImplementation(const protobuf::InterProcessPortalConfig& cfg) : cfg_(cfg)
    {
        _init();
    }

    InterProcessPortalImplementation(InnerTransporter& inner,
                                     const protobuf::InterProcessPortalConfig& cfg)
        : Base(inner), cfg_(cfg)
    {
        _init();
    }

    ~InterProcessPortalImplementation()
    {
        // undeclare every subscriber before the state its callback touches goes away
        subscribers_.clear();
        wildcard_subscriber_.reset();
        session_.reset();
    }

    // no hold implemented: Zenoh peers discover each other without a broker to mediate a hold
    void ready() {}
    bool hold_state() { return false; }

    friend Base;
    friend typename Base::Base;
    friend typename Base::Common;

  private:
    void _init()
    {
        goby::glog.set_lock_action(goby::util::logger_lock::lock);
        key_root_ = make_key_root(cfg_, ImplementationTag::layer);
        session_ = std::make_unique<::zenoh::Session>(open_session(cfg_));

        goby::glog.is_debug1() && goby::glog << "Zenoh: session open, key root: " << key_root_
                                             << std::endl;
    }

    void _do_publish(const std::string& identifier, const std::vector<char>& bytes)
    {
        const std::string key = detail::identifier_to_key(key_root_, identifier, false);

        goby::glog.is_debug3() && goby::glog << "Zenoh: publishing " << bytes.size() << "B to "
                                             << key << std::endl;

        ::zenoh::Session::PutOptions options;
        options.congestion_control =
            cfg_.congestion_control() == protobuf::InterProcessPortalConfig::DROP
                ? ::zenoh::CongestionControl::Z_CONGESTION_CONTROL_DROP
                : ::zenoh::CongestionControl::Z_CONGESTION_CONTROL_BLOCK;
        options.is_express = cfg_.express();

        session_->put(::zenoh::KeyExpr(key),
                      ::zenoh::Bytes(std::string(bytes.begin(), bytes.end())), std::move(options));
    }

    void _do_portal_subscribe(const std::string& identifier)
    {
        portal_identifiers_.insert(identifier);
        // the wildcard subscriber already covers this key, and a second matching subscriber would
        // deliver every sample twice
        if (!wildcard_subscriber_)
            _declare(identifier);
    }

    void _do_portal_unsubscribe(const std::string& identifier)
    {
        portal_identifiers_.erase(identifier);
        subscribers_.erase(identifier);
    }

    void _do_portal_wildcard_subscribe()
    {
        subscribers_.clear();
        wildcard_subscriber_ =
            std::make_unique<::zenoh::Subscriber<void>>(_make_subscriber(key_root_ + "/**"));
    }

    void _do_portal_wildcard_unsubscribe()
    {
        wildcard_subscriber_.reset();
        for (const auto& identifier : portal_identifiers_) _declare(identifier);
    }

    void _declare(const std::string& identifier)
    {
        const std::string key = detail::identifier_to_key(key_root_, identifier, true);
        subscribers_.erase(identifier);
        subscribers_.emplace(identifier, _make_subscriber(key));
    }

    ::zenoh::Subscriber<void> _make_subscriber(const std::string& key)
    {
        goby::glog.is_debug2() && goby::glog << "Zenoh: subscribing to " << key << std::endl;

        return session_->declare_subscriber(
            ::zenoh::KeyExpr(key), [this](const ::zenoh::Sample& sample) { _on_sample(sample); },
            ::zenoh::closures::none);
    }

    // runs on a Zenoh thread
    void _on_sample(const ::zenoh::Sample& sample)
    {
        std::string identifier = detail::key_to_identifier(
            key_root_, std::string(sample.get_keyexpr().as_string_view()));
        if (identifier.empty())
        {
            goby::glog.is_warn() && goby::glog << "Zenoh: received sample outside the key root: "
                                               << sample.get_keyexpr().as_string_view()
                                               << std::endl;
            return;
        }

        // the layer above expects the identifier, its end delimiter and the payload as one string
        identifier += middleware::InterProcessIdentifierManager::end_delimiter;
        identifier += sample.get_payload().as_string();

        {
            std::lock_guard<std::mutex> lock(rx_mutex_);
            rx_.push_back(std::move(identifier));
        }
        middleware::detail::notify_poller(*this->poll_mutex(), *this->cv());
    }

    int _poll(std::unique_ptr<std::unique_lock<std::mutex>>& lock)
    {
        std::deque<std::string> received;
        {
            std::lock_guard<std::mutex> rx_lock(rx_mutex_);
            received.swap(rx_);
        }

        int items = 0;
        for (const auto& data : received)
        {
            ++items;
            this->_handle_received_data(lock, data);
        }
        return items;
    }

  private:
    const protobuf::InterProcessPortalConfig cfg_;
    std::string key_root_;
    std::unique_ptr<::zenoh::Session> session_;

    // main thread only
    std::set<std::string> portal_identifiers_;
    std::unordered_map<std::string, ::zenoh::Subscriber<void>> subscribers_;
    std::unique_ptr<::zenoh::Subscriber<void>> wildcard_subscriber_;

    // written by Zenoh threads, drained by the main thread in _poll()
    std::mutex rx_mutex_;
    std::deque<std::string> rx_;
};

template <typename InnerTransporter = middleware::NullTransporter>
using InterProcessPortal =
    InterProcessPortalImplementation<InnerTransporter, middleware::InterProcessPortalBase,
                                     detail::InterProcessTag>;

template <typename InnerTransporter = middleware::NullTransporter>
using InterProcessForwarder =
    middleware::InterProcessForwarder<InnerTransporter, detail::InterProcessTag>;

} // namespace zenoh
} // namespace goby

namespace goby
{
namespace middleware
{
namespace detail
{
template <> struct implementation_traits<goby::zenoh::detail::InterProcessTag>
{
    template <typename InnerTransporter>
    using Portal = goby::zenoh::InterProcessPortal<InnerTransporter>;
    using PortalConfig = goby::zenoh::protobuf::InterProcessPortalConfig;
    static constexpr const char* name = "zenoh";
};
} // namespace detail
} // namespace middleware
} // namespace goby

#endif
