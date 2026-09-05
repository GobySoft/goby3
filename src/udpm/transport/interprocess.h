// Copyright 2026:
//   GobySoft, LLC (2013-)
//   Community contributors (see AUTHORS file)
// File authors:
//   Copilot <198982749+Copilot@users.noreply.github.com>
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

#include <arpa/inet.h>
#include <array>
#include <boost/asio/buffer.hpp>
#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/ip/multicast.hpp>
#include <boost/asio/ip/udp.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/socket_base.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/circular_buffer.hpp>
#include <chrono>
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <set>
#include <thread>
#include <unordered_map>

#include "goby/middleware/transport/detail/implementation_traits.h"
#include "goby/middleware/transport/identifier.h"
#include "goby/middleware/transport/interface.h"
#include "goby/middleware/transport/interprocess.h"
#include "goby/util/debug_logger.h"

#include "goby/udpm/protobuf/interprocess_config.pb.h"
#include "goby/udpm/transport/detail/tags.h"

namespace goby
{
namespace middleware
{
template <typename Data> class Publisher;
} // namespace middleware

namespace udpm
{

// Packet status codes
enum class UDPMPacketStatus : uint8_t
{
    NORMAL = 0,
};

// Packet header (9 bytes, after the null-terminated identifier):
//   message_index  : uint32 (network byte order)
//   num_packets    : uint16 (network byte order)
//   packet_count   : uint16 (network byte order)
//   status         : uint8
static constexpr std::size_t UDPM_PACKET_HEADER_SIZE =
    sizeof(uint32_t) + sizeof(uint16_t) + sizeof(uint16_t) + sizeof(uint8_t);

struct UDPMPacketHeader
{
    uint32_t message_index{0};
    uint16_t num_packets{1};
    uint16_t packet_count{0};
    UDPMPacketStatus status{UDPMPacketStatus::NORMAL};
};

inline void encode_header(char* buf, const UDPMPacketHeader& h)
{
    uint32_t mi = htonl(h.message_index);
    uint16_t np = htons(h.num_packets);
    uint16_t pc = htons(h.packet_count);
    std::memcpy(buf, &mi, 4);
    std::memcpy(buf + 4, &np, 2);
    std::memcpy(buf + 6, &pc, 2);
    buf[8] = static_cast<uint8_t>(h.status);
}

inline UDPMPacketHeader decode_header(const char* buf)
{
    UDPMPacketHeader h;
    uint32_t mi;
    uint16_t np, pc;
    std::memcpy(&mi, buf, 4);
    std::memcpy(&np, buf + 4, 2);
    std::memcpy(&pc, buf + 6, 2);
    h.message_index = ntohl(mi);
    h.num_packets = ntohs(np);
    h.packet_count = ntohs(pc);
    h.status = static_cast<UDPMPacketStatus>(static_cast<uint8_t>(buf[8]));
    return h;
}

template <typename InnerTransporter,
          template <typename Derived, typename InnerTransporterType,
                    typename ImplementationTag_> class PortalBase,
          typename ImplementationTag>
class InterProcessPortalImplementation
    : public PortalBase<InterProcessPortalImplementation<InnerTransporter, PortalBase,
                                                        ImplementationTag>,
                        InnerTransporter, ImplementationTag>
{
  public:
    using Base = PortalBase<InterProcessPortalImplementation<InnerTransporter, PortalBase,
                                                            ImplementationTag>,
                            InnerTransporter, ImplementationTag>;

    using IdentifierWildcard = middleware::IdentifierWildcard;

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
        // Close socket first to cancel any pending async operations,
        // then release the work guard so io_.ctx.run() can return, then join.
        io_.socket.close();
        io_.work.reset();
        if (io_thread_.joinable())
            io_thread_.join();
    }

    // no-op - no hold implemented in UDPM
    void ready() {}
    bool hold_state() { return false; }

    friend Base;
    friend typename Base::Base;
    friend typename Base::Common;

  private:
    // -----------------------------------------------------------------------
    // Called from main thread before io_thread_ starts; sets up socket and
    // launches io_thread_.
    // -----------------------------------------------------------------------
    void _init()
    {
        goby::glog.set_lock_action(goby::util::logger_lock::lock);

        boost::asio::ip::address listen_address =
            boost::asio::ip::make_address(cfg_.listen_address());
        boost::asio::ip::address multicast_address =
            boost::asio::ip::make_address(cfg_.multicast_address());
        short multicast_port = static_cast<short>(cfg_.multicast_port());

        boost::asio::ip::udp::endpoint listen_endpoint(listen_address, multicast_port);
        io_.transmit_endpoint = boost::asio::ip::udp::endpoint(multicast_address, multicast_port);

        io_.socket.open(listen_endpoint.protocol());
        io_.socket.set_option(boost::asio::ip::udp::socket::reuse_address(true));
        io_.socket.bind(listen_endpoint);
        io_.socket.set_option(boost::asio::ip::multicast::join_group(multicast_address));

        // Arm the first async receive before starting io_thread_ so the
        // operation is already queued when io_.ctx.run() is called.
        _start_async_receive();
        io_thread_ = std::thread([this]() { io_.ctx.run(); });
    }

    // -----------------------------------------------------------------------
    // Runs on io_thread_: arms (or re-arms) a single async datagram receive.
    // Called once from _init() (main thread, before io_thread_ starts) and
    // then re-posted from within the completion handler on io_thread_.
    // -----------------------------------------------------------------------
    void _start_async_receive()
    {
        io_.socket.async_receive_from(
            boost::asio::buffer(io_.rx_buffer), io_.sender_endpoint,
            [this](boost::system::error_code ec, std::size_t length)
            {
                if (!ec)
                {
                    {
                        std::lock_guard<std::mutex> l(rx_mutex_);
                        rx_.push_back(std::string(io_.rx_buffer.begin(),
                                                  io_.rx_buffer.begin() + length));
                    }
                    // Acquire poll_mutex briefly to ensure the main thread is not in the
                    // limbo region between _poll_all() releasing the lock and calling
                    // cv_.wait(). Without this, notify_all() could be missed.
                    {
                        std::lock_guard<std::mutex> l(*this->poll_mutex());
                    }
                    this->cv()->notify_all();
                    _start_async_receive();
                }
            });
    }

    // -----------------------------------------------------------------------
    // Main thread helpers
    // -----------------------------------------------------------------------

    std::shared_ptr<std::vector<char>> _build_packet(const std::string& identifier,
                                                     const UDPMPacketHeader& hdr,
                                                     const char* data_begin, std::size_t data_len)
    {
        auto pkt = std::make_shared<std::vector<char>>();
        pkt->reserve(identifier.size() + UDPM_PACKET_HEADER_SIZE + data_len);
        pkt->insert(pkt->end(), identifier.begin(), identifier.end());
        char hdr_buf[UDPM_PACKET_HEADER_SIZE];
        encode_header(hdr_buf, hdr);
        pkt->insert(pkt->end(), hdr_buf, hdr_buf + UDPM_PACKET_HEADER_SIZE);
        if (data_len > 0)
            pkt->insert(pkt->end(), data_begin, data_begin + data_len);
        return pkt;
    }

    void _async_send(std::shared_ptr<std::vector<char>> pkt)
    {
        if (cfg_.max_send_rate_bytes_per_second() > 0)
        {
            auto now = std::chrono::steady_clock::now();

            std::this_thread::sleep_until(next_send_time_);

            double delay_sec = static_cast<double>(pkt->size()) /
                               static_cast<double>(cfg_.max_send_rate_bytes_per_second());

            if (static_cast<long>(delay_sec * 1.0e9) < std::numeric_limits<long>::max())
                next_send_time_ =
                    now + std::chrono::nanoseconds(static_cast<long>(1e9 * delay_sec));
            else if (static_cast<long>(delay_sec * 1.0e6) < std::numeric_limits<long>::max())

                next_send_time_ =
                    now + std::chrono::microseconds(static_cast<long>(1e6 * delay_sec));
            else
                next_send_time_ =
                    now + std::chrono::milliseconds(static_cast<long>(1e3 * delay_sec));
        }

        _do_socket_send(std::move(pkt));
    }

    void _do_socket_send(std::shared_ptr<std::vector<char>> pkt)
    {
        // Post the actual socket write to io_thread_ so that the socket is
        // only ever touched from io_thread_.
        boost::asio::post(io_.ctx,
                          [this, pkt = std::move(pkt)]()
                          {
                              io_.socket.async_send_to(
                                  boost::asio::buffer(*pkt), io_.transmit_endpoint,
                                  [pkt](boost::system::error_code ec, std::size_t length)
                                  {
                                      if (!ec)
                                          goby::glog.is_debug3() &&
                                              goby::glog << "UDPM: Sent " << length << "B"
                                                         << std::endl;
                                      else
                                          goby::glog.is_warn() &&
                                              goby::glog << "UDPM: Send error: " << ec.message()
                                                         << std::endl;
                                  });
                          });
    }

    void _do_publish(const std::string& identifier, const std::vector<char>& bytes)
    {
        goby::glog.is_debug3() &&
            goby::glog << "UDPM: Publishing for: "
                       << std::string(identifier.begin(),
                                      std::find(identifier.begin(), identifier.end(), '\0'))
                       << ", " << bytes.size() << "B" << std::endl;

        const std::size_t header_overhead = identifier.size() + UDPM_PACKET_HEADER_SIZE;
        const std::size_t payload_bytes = cfg_.udp_payload_bytes();

        std::string id_key(identifier.begin(),
                           std::find(identifier.begin(), identifier.end(), '\0'));

        uint32_t msg_idx = tx_message_index_[id_key]++;

        // single packet for message
        if (header_overhead + bytes.size() <= payload_bytes)
        {
            goby::glog.is_debug3() && goby::glog << "UDPM: Sent in a single packet" << std::endl;

            UDPMPacketHeader hdr;
            hdr.message_index = msg_idx;
            hdr.num_packets = 1;
            hdr.packet_count = 0;
            hdr.status = UDPMPacketStatus::NORMAL;

            auto pkt = _build_packet(identifier, hdr, bytes.data(), bytes.size());

            TxMessageEntry entry;
            entry.message_index = msg_idx;
            entry.packets.push_back(pkt);

            _async_send(pkt);
            return;
        }

        const std::size_t max_data_per_packet = payload_bytes - header_overhead;
        const std::size_t num_packets_needed =
            (bytes.size() + max_data_per_packet - 1) / max_data_per_packet;

        if (num_packets_needed > std::numeric_limits<uint16_t>::max())
        {
            goby::glog.is_warn() &&
                goby::glog << "UDPM: Message too large to packetize: " << bytes.size() << " bytes, "
                           << num_packets_needed << " packets needed" << std::endl;
        }

        const uint16_t num_pkts = static_cast<uint16_t>(std::min(
            num_packets_needed, static_cast<std::size_t>(std::numeric_limits<uint16_t>::max())));

        TxMessageEntry entry;
        entry.message_index = msg_idx;
        entry.packets.reserve(num_pkts);

        goby::glog.is_debug3() && goby::glog << "UDPM: Sending in " << num_pkts << " packets"
                                             << std::endl;
        for (uint16_t i = 0; i < num_pkts; ++i)
        {
            std::size_t offset = static_cast<std::size_t>(i) * max_data_per_packet;
            std::size_t chunk = std::min(max_data_per_packet, bytes.size() - offset);

            UDPMPacketHeader hdr;
            hdr.message_index = msg_idx;
            hdr.num_packets = num_pkts;
            hdr.packet_count = i;
            hdr.status = UDPMPacketStatus::NORMAL;

            auto pkt = _build_packet(identifier, hdr, bytes.data() + offset, chunk);
            entry.packets.push_back(pkt);
            _async_send(pkt);
        }
    }

    void _do_portal_subscribe(const std::string& identifier)
    {
        goby::glog.is_debug3() && goby::glog << "UDPM: Subscribe for: " << identifier << " (no-op)"
                                             << std::endl;
    }
    void _do_portal_unsubscribe(const std::string& identifier)
    {
        goby::glog.is_debug3() && goby::glog << "UDPM: Unsubscribe for: " << identifier
                                             << " (no-op)" << std::endl;
    }
    void _do_portal_wildcard_subscribe()
    {
        goby::glog.is_debug3() && goby::glog << "UDPM: Wildcard subscribe (no-op)" << std::endl;
    }
    void _do_portal_wildcard_unsubscribe()
    {
        goby::glog.is_debug3() && goby::glog << "UDPM: Wildcard unsubscribe (no-op)" << std::endl;
    }

    void _process_received_packet(std::unique_ptr<std::unique_lock<std::mutex>>& lock,
                                  const std::string& raw)
    {
        auto null_it = std::find(raw.begin(), raw.end(),
                                 middleware::InterProcessIdentifierManager::end_delimiter);
        if (null_it == raw.end())
        {
            goby::glog.is_warn() && goby::glog
                                        << "UDPM: Received packet with no null terminator, dropping"
                                        << std::endl;
            return;
        }

        std::string id_key(raw.begin(), null_it);

        auto header_start = null_it + 1;
        if (raw.end() - header_start < static_cast<std::ptrdiff_t>(UDPM_PACKET_HEADER_SIZE))
        {
            goby::glog.is_warn() &&
                goby::glog << "UDPM: Received packet too short for header, dropping" << std::endl;
            return;
        }

        UDPMPacketHeader hdr = decode_header(&*header_start);
        auto data_begin = header_start + UDPM_PACKET_HEADER_SIZE;

        goby::glog.is_debug3() &&
            goby::glog << "UDPM: Received packet for " << id_key << " msg_idx=" << hdr.message_index
                       << " num_pkts=" << hdr.num_packets << " pkt_cnt=" << hdr.packet_count
                       << " status=" << static_cast<int>(hdr.status)
                       << " data=" << (raw.end() - data_begin) << "B" << std::endl;

        if (hdr.num_packets == 1)
        {
            std::string full_data(raw.begin(), null_it + 1);
            full_data.append(data_begin, raw.end());
            this->_handle_received_data(lock, full_data);
            return;
        }

        auto& partial = rx_partial_[id_key];
        if (partial.message_index != hdr.message_index)
        {
            goby::glog.is_warn() && goby::glog << "UDPM: Dropping partial packet for " << id_key
                                               << " msg_idx=" << hdr.message_index << std::endl;
            partial = RxPartialMessage();
        }

        if (partial.num_packets == 0)
            partial.num_packets = hdr.num_packets;

        partial.received_packets[hdr.packet_count] = std::vector<char>(data_begin, raw.end());

        if (partial.received_packets.size() == hdr.num_packets)
        {
            std::string reassembled(raw.begin(), null_it + 1);
            for (uint16_t i = 0; i < hdr.num_packets; ++i)
            {
                auto& pkt = partial.received_packets[i];
                reassembled.append(pkt.begin(), pkt.end());
            }
            partial = RxPartialMessage();
            this->_handle_received_data(lock, reassembled);
        }
    }

    int _poll(std::unique_ptr<std::unique_lock<std::mutex>>& lock)
    {
        int items = 0;

        std::deque<std::string> local_rx;
        {
            std::lock_guard<std::mutex> l(rx_mutex_);
            local_rx.swap(rx_);
        }

        while (!local_rx.empty())
        {
            ++items;
            _process_received_packet(lock, local_rx.front());
            local_rx.pop_front();
        }

        return items;
    }

  private:
    struct RxPartialMessage
    {
        uint32_t message_index{0};
        uint16_t num_packets{0};
        std::unordered_map<uint16_t, std::vector<char>> received_packets;
    };

    struct TxMessageEntry
    {
        uint32_t message_index{0};
        std::vector<std::shared_ptr<std::vector<char>>> packets;
    };

    // -----------------------------------------------------------------------
    // Read-only configuration: written once in constructor, then safe to read
    // from any thread without synchronization.
    // -----------------------------------------------------------------------
    const protobuf::InterProcessPortalConfig cfg_;

    // -----------------------------------------------------------------------
    // io_thread_ exclusive state
    //
    // All members of IOState are accessed ONLY from io_thread_, or from
    // _init() before io_thread_ is started. Do NOT access these directly
    // from the main thread once io_thread_ is running.
    //
    // Methods that run on io_thread_:
    //   _start_async_receive()  -- arms / re-arms async receives
    //   async_receive_from completion handler  -- pushes to rx_ then notifies
    //   async_send_to operation  -- posted by _do_socket_send() via boost::asio::post
    // -----------------------------------------------------------------------
    struct IOState
    {
        boost::asio::io_context ctx;
        boost::asio::ip::udp::socket socket{ctx};
        boost::asio::executor_work_guard<boost::asio::io_context::executor_type> work{
            ctx.get_executor()};
        boost::asio::ip::udp::endpoint sender_endpoint;   // updated by async_receive_from
        boost::asio::ip::udp::endpoint transmit_endpoint; // set once in _init, then read-only

        static constexpr int max_udp_size{65507};
        std::array<char, max_udp_size> rx_buffer{}; // staging buffer for incoming datagrams
    } io_;

    // -----------------------------------------------------------------------
    // Shared state: written by io_thread_, read (swapped) by main thread.
    //
    // rx_  : io_thread_ pushes completed datagrams; main thread swaps it out
    //        in _poll(). All accesses must hold rx_mutex_.
    // -----------------------------------------------------------------------
    std::mutex rx_mutex_;
    std::deque<std::string> rx_;

    // -----------------------------------------------------------------------
    // Main thread exclusive state
    //
    // These members are accessed ONLY from the main thread.
    //
    // Methods that run on the main thread:
    //   _init()                        -- setup; starts io_thread_
    //   _build_packet()                -- serializes outgoing datagrams
    //   _async_send()                  -- rate-limiter; calls _do_socket_send()
    //   _do_socket_send()              -- posts async_send_to to io_thread_
    //   _do_publish()                  -- fragments and enqueues outgoing messages
    //   _do_portal_subscribe/unsubscribe/wildcard_*  -- subscription bookkeeping
    //   _process_received_packet()     -- reassembles and dispatches incoming data
    //   _poll()                        -- drains rx_ and calls _process_received_packet
    // -----------------------------------------------------------------------
    std::unordered_map<std::string, uint32_t> tx_message_index_;
    std::unordered_map<std::string, RxPartialMessage> rx_partial_;
    std::chrono::steady_clock::time_point next_send_time_{std::chrono::steady_clock::now()};

    // -----------------------------------------------------------------------
    // Thread management
    // -----------------------------------------------------------------------
    std::thread io_thread_; // runs io_.ctx.run()
};

template <typename InnerTransporter = middleware::NullTransporter>
using InterProcessPortal =
    InterProcessPortalImplementation<InnerTransporter, middleware::InterProcessPortalBase,
                                     detail::InterProcessTag>;

template <typename InnerTransporter = middleware::NullTransporter>
using InterProcessForwarder =
    middleware::InterProcessForwarder<InnerTransporter, detail::InterProcessTag>;

} // namespace udpm
} // namespace goby

namespace goby
{
namespace middleware
{
namespace detail
{
template <> struct implementation_traits<goby::udpm::detail::InterProcessTag>
{
    template <typename InnerTransporter>
    using Portal = goby::udpm::InterProcessPortal<InnerTransporter>;
    using PortalConfig = goby::udpm::protobuf::InterProcessPortalConfig;
    static constexpr const char* name = "udpm";
};
} // namespace detail
} // namespace middleware
} // namespace goby

#endif
