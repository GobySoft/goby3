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

#include <arpa/inet.h>
#include <array>
#include <boost/asio/buffer.hpp>
#include <boost/asio/ip/multicast.hpp>
#include <boost/asio/ip/udp.hpp>
#include <boost/asio/socket_base.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/circular_buffer.hpp>
#include <chrono>
#include <cstring>
#include <deque>
#include <memory>
#include <set>
#include <unordered_map>

#include "goby/middleware/transport/identifier.h"
#include "goby/middleware/transport/interface.h"
#include "goby/middleware/transport/interprocess.h"
#include "goby/util/debug_logger.h"

#include "goby/udpm/protobuf/interprocess_config.pb.h"

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
        : Base(inner), cfg_(cfg)
    {
        _init();
    }

    ~InterProcessPortalImplementation() { socket_.close(); }

    // no-op - no hold implemented in UDPM
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
        short multicast_port = static_cast<short>(cfg_.multicast_port());

        boost::asio::ip::udp::endpoint listen_endpoint(listen_address, multicast_port);
        transmit_endpoint_ = boost::asio::ip::udp::endpoint(multicast_address, multicast_port);

        socket_.open(listen_endpoint.protocol());
        socket_.set_option(boost::asio::ip::udp::socket::reuse_address(true));
        socket_.bind(listen_endpoint);
        socket_.set_option(boost::asio::ip::multicast::join_group(multicast_address));

        _start_async_receive();
    }

    void _start_async_receive()
    {
        socket_.async_receive_from(
            boost::asio::buffer(rx_buffer_), sender_endpoint_,
            [this](boost::system::error_code ec, std::size_t length)
            {
                if (!ec)
                {
                    rx_.push_back(std::string(rx_buffer_.begin(), rx_buffer_.begin() + length));
                    _start_async_receive();
                }
            });
    }

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
        socket_.async_send_to(boost::asio::buffer(*pkt), transmit_endpoint_,
                              [pkt](boost::system::error_code ec, std::size_t length)
                              {
                                  if (!ec)
                                      goby::glog.is_debug3() &&
                                          goby::glog << "UDPM: Sent " << length << "B" << std::endl;
                                  else
                                      goby::glog.is_warn() &&
                                          goby::glog << "UDPM: Send error: " << ec.message()
                                                     << std::endl;
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
        io_.poll();

        while (!rx_.empty())
        {
            ++items;
            _process_received_packet(lock, rx_.front());
            rx_.pop_front();
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

    const protobuf::InterProcessPortalConfig cfg_;

    boost::asio::io_context io_;
    boost::asio::ip::udp::socket socket_{io_};
    boost::asio::ip::udp::endpoint sender_endpoint_;
    boost::asio::ip::udp::endpoint transmit_endpoint_;

    static constexpr int max_udp_size{65507};
    std::array<char, max_udp_size> rx_buffer_;
    std::deque<std::string> rx_;

    std::unordered_map<std::string, uint32_t> tx_message_index_;
    std::unordered_map<std::string, RxPartialMessage> rx_partial_;

    // Rate-limiting send queue
    std::chrono::steady_clock::time_point next_send_time_{std::chrono::steady_clock::now()};
};

template <typename InnerTransporter = middleware::NullTransporter>
using InterProcessPortal =
    InterProcessPortalImplementation<InnerTransporter, middleware::InterProcessPortalBase>;

} // namespace udpm
} // namespace goby

#endif
