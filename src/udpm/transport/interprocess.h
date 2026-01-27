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

#ifndef GOBY_UDPM_TRANSPORT_INTERPROCESS_H
#define GOBY_UDPM_TRANSPORT_INTERPROCESS_H

#include <array>
#include <boost/asio/buffer.hpp>
#include <boost/asio/ip/multicast.hpp>
#include <boost/asio/ip/udp.hpp>
#include <boost/asio/socket_base.hpp>

#include "goby/middleware/transport/identifier.h"
#include "goby/middleware/transport/interface.h"
#include "goby/middleware/transport/interprocess.h"

#include "goby/udpm/protobuf/interprocess_config.pb.h"

namespace goby
{
namespace middleware
{
template <typename Data> class Publisher;
} // namespace middleware

namespace udpm
{

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

    InterProcessPortalImplementation(const protobuf::InterProcessPortalConfig& cfg) : cfg_(cfg)
    {
        _init();
    }

    InterProcessPortalImplementation(InnerTransporter& inner,
                                     const protobuf::InterProcessPortalConfig& cfg)
        : InterProcessPortalImplementation(cfg)
    {
    }

    ~InterProcessPortalImplementation() {}

    // no-op - no hold implemented in UDPm
    void ready() {}
    bool hold_state() { return false; }

    friend Base;
    friend typename Base::Base;
    friend typename Base::Common;

  private:
    void _init()
    {
        goby::glog.set_lock_action(goby::util::logger_lock::lock);
        boost::asio::ip::address listen_address =
            boost::asio::ip::make_address(cfg_.listen_address());
        boost::asio::ip::address multicast_address =
            boost::asio::ip::make_address(cfg_.multicast_address());

        short multicast_port = cfg_.multicast_port();

        boost::asio::ip::udp::endpoint listen_endpoint(listen_address, multicast_port);
        transmit_endpoint_ = boost::asio::ip::udp::endpoint(multicast_address, multicast_port);

        socket_.open(listen_endpoint.protocol());
        socket_.set_option(boost::asio::ip::udp::socket::reuse_address(true));
        socket_.bind(listen_endpoint);

        socket_.set_option(boost::asio::ip::multicast::join_group(multicast_address));

        start_async_receive();
    }

    void start_async_receive()
    {
        socket_.async_receive_from(
            boost::asio::buffer(rx_buffer_), sender_endpoint_,
            [this](boost::system::error_code ec, std::size_t length)
            {
                if (!ec)
                {
                    auto null_it =
                        std::find(rx_buffer_.begin(), rx_buffer_.end(),
                                  middleware::InterProcessIdentifierManager::end_delimiter);

                    goby::glog.is_debug3() &&
                        goby::glog << "UDPM: Received " << length
                                   << "B: " << std::string(rx_buffer_.begin(), null_it) << ": "
                                   << goby::util::hex_encode(
                                          std::string(null_it + 1, rx_buffer_.begin() + length))
                                   << std::endl;

                    rx_.push_back(std::string(rx_buffer_.begin(), rx_buffer_.begin() + length));
                    start_async_receive();
                }
            });
    }

    void _do_publish(const std::string& identifier, const std::vector<char>& bytes)
    {
        goby::glog.is_debug3() && goby::glog << "UDPM: Asked to publish for: " << identifier
                                             << std::endl;

        socket_.async_send_to(std::array<boost::asio::const_buffer, 2>(
                                  {boost::asio::buffer(identifier), boost::asio::buffer(bytes)}),
                              transmit_endpoint_,
                              [](boost::system::error_code ec, std::size_t length)
                              {
                                  if (!ec)
                                      goby::glog.is_debug3() &&
                                          goby::glog << "UDPM: Sent " << length << "B" << std::endl;
                              });
    }

    void _do_portal_subscribe(const std::string& identifier)
    {
        goby::glog.is_debug3() && goby::glog << "UDPM: Asked to subscribe for: " << identifier
                                             << ", nothing to do" << std::endl;
    }
    void _do_portal_unsubscribe(const std::string& identifier)
    {
        goby::glog.is_debug3() && goby::glog << "UDPM: Asked to unsubscribe for: " << identifier
                                             << ", nothing to do" << std::endl;
    }

    void _do_portal_wildcard_subscribe()
    {
        goby::glog.is_debug3() && goby::glog << "UDPM: Asked to wildcard subscribe, nothing to do"
                                             << std::endl;
    }
    void _do_portal_wildcard_unsubscribe()
    {
        goby::glog.is_debug3() && goby::glog << "UDPM: Asked to wildcard unsubscribe, nothing to do"
                                             << std::endl;
    }

    int _poll(std::unique_ptr<std::unique_lock<std::timed_mutex>>& lock)
    {
        int items = 0;
        io_.poll();

        while (!rx_.empty())
        {
            ++items;
            this->_handle_received_data(lock, rx_.front());
            rx_.pop_front();
        }

        return items;
    }

  private:
    const protobuf::InterProcessPortalConfig cfg_;

    boost::asio::io_context io_;
    boost::asio::ip::udp::socket socket_{io_};
    boost::asio::ip::udp::endpoint sender_endpoint_;
    boost::asio::ip::udp::endpoint transmit_endpoint_;

    static constexpr int max_udp_size{65507};
    std::array<char, max_udp_size> rx_buffer_;
    std::deque<std::string> rx_;
};

template <typename InnerTransporter = middleware::NullTransporter>
using InterProcessPortal =
    InterProcessPortalImplementation<InnerTransporter, middleware::InterProcessPortalBase>;

} // namespace udpm
} // namespace goby

#endif
