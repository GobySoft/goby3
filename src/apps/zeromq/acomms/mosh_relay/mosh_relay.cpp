// Copyright 2009-2018 Toby Schneider (http://gobysoft.org/index.wt/people/toby)
//                     GobySoft, LLC (2013-)
//                     Massachusetts Institute of Technology (2007-2014)
//
//
// This file is part of the Goby Underwater Autonomy Project Binaries
// ("The Goby Binaries").
//
// The Goby Binaries are free software: you can redistribute them and/or modify
// them under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 2 of the License, or
// (at your option) any later version.
//
// The Goby Binaries are distributed in the hope that they will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with Goby.  If not, see <http://www.gnu.org/licenses/>.

#include <boost/asio.hpp>

#include "goby/middleware/marshalling/protobuf.h"

#include "goby/acomms/protobuf/mosh_packet.pb.h"
#include "goby/middleware/acomms/groups.h"
#include "goby/zeromq/application/single_thread.h"
#include "goby/zeromq/protobuf/mosh_relay_config.pb.h"

using goby::glog;
using namespace goby::util::logger;
using goby::acomms::protobuf::MoshPacket;

using boost::asio::ip::udp;

namespace goby
{
namespace apps
{
namespace zeromq
{
namespace acomms
{
class Packetizer
{
  public:
    Packetizer();
    Packetizer(int src, int dest, const std::vector<char>& input);
    const std::set<MoshPacket>& fragments() { return fragments_; }

    bool add_fragment(const MoshPacket& frag);
    std::vector<char> reassemble();

  private:
    std::set<MoshPacket> fragments_;
};

class MoshRelay : public goby::zeromq::SingleThreadApplication<protobuf::MoshRelayConfig>

{
  public:
    MoshRelay();
    ~MoshRelay();

  private:
    void loop();
    void start_udp_receive();
    void handle_udp_receive(const boost::system::error_code& error, std::size_t bytes_transferred);
    void handle_goby_receive(const MoshPacket& pkt);
    void handle_udp_send(const boost::system::error_code& error, std::size_t bytes_transferred);

  private:
    boost::asio::io_service io_service_;
    udp::socket socket_;

    enum
    {
        MOSH_UDP_PAYLOAD_SIZE = 1300
    };

    udp::endpoint remote_endpoint_;
    std::vector<char> recv_buffer_;

    typedef int ModemId;
    std::map<ModemId, Packetizer> packets_;

    goby::middleware::DynamicGroup queue_rx_group_;
    goby::middleware::DynamicGroup queue_push_group_;
};

} // namespace acomms
} // namespace zeromq
} // namespace apps

namespace acomms
{
namespace protobuf
{
inline bool operator<(const MoshPacket& a, const MoshPacket& b)
{
    return a.frag_num() < b.frag_num();
}
} // namespace protobuf
} // namespace acomms
} // namespace goby

const int MOSH_FRAGMENT_SIZE = MoshPacket::descriptor()
                                   ->FindFieldByName("fragment")
                                   ->options()
                                   .GetExtension(dccl::field)
                                   .max_length();

void test_packetizer(int size)
{
    std::vector<char> in(size, 0);
    for (int i = 0; i < size; ++i) in[i] = i % 256;
    goby::apps::zeromq::acomms::Packetizer pi(1, 2, in), po;
    const std::set<MoshPacket>& f = pi.fragments();
    bool ready = false;
    for (std::set<MoshPacket>::const_iterator it = f.begin(), end = f.end(); it != end; ++it)
        ready = po.add_fragment(*it);
    assert(ready);

    assert(po.reassemble() == in);
}

int main(int argc, char* argv[])
{
    test_packetizer(MOSH_FRAGMENT_SIZE * 4);
    test_packetizer(1300);
    test_packetizer(MOSH_FRAGMENT_SIZE - 10);
    test_packetizer(MOSH_FRAGMENT_SIZE * 2 - 5);

    goby::run<goby::apps::zeromq::acomms::MoshRelay>(argc, argv);
}

using goby::glog;

goby::apps::zeromq::acomms::MoshRelay::MoshRelay()
    : goby::zeromq::SingleThreadApplication<protobuf::MoshRelayConfig>(10 *
                                                                       boost::units::si::hertz),
      socket_(io_service_),
      recv_buffer_(MOSH_UDP_PAYLOAD_SIZE, 0),
      queue_rx_group_(goby::middleware::acomms::groups::queue_rx, cfg().src_modem_id()),
      queue_push_group_(goby::middleware::acomms::groups::queue_push, cfg().src_modem_id())
{
    socket_.open(udp::v4());
    if (cfg().bind())
    {
        socket_.bind(udp::endpoint(boost::asio::ip::address::from_string(cfg().ip_address()),
                                   cfg().udp_port()));
    }
    else
    {
        remote_endpoint_ = udp::endpoint(boost::asio::ip::address::from_string(cfg().ip_address()),
                                         cfg().udp_port());
    }
    start_udp_receive();

    interprocess().subscribe_dynamic<MoshPacket>(
        std::bind(&MoshRelay::handle_goby_receive, this, std::placeholders::_1), queue_rx_group_);
}

goby::apps::zeromq::acomms::MoshRelay::~MoshRelay() {}

void goby::apps::zeromq::acomms::MoshRelay::loop() { io_service_.poll(); }

void goby::apps::zeromq::acomms::MoshRelay::start_udp_receive()
{
    socket_.async_receive_from(boost::asio::buffer(recv_buffer_), remote_endpoint_,
                               boost::bind(&MoshRelay::handle_udp_receive, this,
                                           boost::asio::placeholders::error,
                                           boost::asio::placeholders::bytes_transferred));
}

void goby::apps::zeromq::acomms::MoshRelay::handle_udp_receive(
    const boost::system::error_code& error, std::size_t bytes_transferred)
{
    if (!error || error == boost::asio::error::message_size)
    {
        glog.is(DEBUG1) && glog << remote_endpoint_ << ": " << bytes_transferred << " Bytes"
                                << std::endl;

        Packetizer p(
            cfg().src_modem_id(), cfg().dest_modem_id(),
            std::vector<char>(recv_buffer_.begin(), recv_buffer_.begin() + bytes_transferred));
        const std::set<MoshPacket>& f = p.fragments();
        for (std::set<MoshPacket>::const_iterator it = f.begin(), end = f.end(); it != end; ++it)
            interprocess().publish_dynamic(*it, queue_push_group_);

        start_udp_receive();
    }
}

void goby::apps::zeromq::acomms::MoshRelay::handle_goby_receive(const MoshPacket& packet)
{
    glog.is(DEBUG1) && glog << "> " << packet.ShortDebugString() << std::endl;

    if (packet.dest() == (int)cfg().src_modem_id() && packet.src() == (int)cfg().dest_modem_id())
    {
        if (packets_[packet.src()].add_fragment(packet))
        {
            socket_.async_send_to(
                boost::asio::buffer(packets_[packet.src()].reassemble()), remote_endpoint_,
                boost::bind(&MoshRelay::handle_udp_send, this, boost::asio::placeholders::error,
                            boost::asio::placeholders::bytes_transferred));
            packets_.erase(packet.src());
        }
    }
}

void goby::apps::zeromq::acomms::MoshRelay::handle_udp_send(
    const boost::system::error_code& /*error*/, std::size_t /*bytes_transferred*/)
{
}

goby::apps::zeromq::acomms::Packetizer::Packetizer() {}

goby::apps::zeromq::acomms::Packetizer::Packetizer(int src, int dest,
                                                   const std::vector<char>& input)
{
    MoshPacket packet;
    packet.set_src(src);
    packet.set_dest(dest);

    for (int i = 0, n = (input.size() + MOSH_FRAGMENT_SIZE - 1) / MOSH_FRAGMENT_SIZE; i < n; ++i)
    {
        packet.set_frag_num(i);
        packet.set_frag_len(
            std::min(MOSH_FRAGMENT_SIZE, (int)(input.size() - i * MOSH_FRAGMENT_SIZE)));
        packet.set_is_last_frag(i + 1 == n);
        std::string* frag = packet.mutable_fragment();
        frag->resize(MOSH_FRAGMENT_SIZE);

        std::vector<char>::const_iterator begin = input.begin() + i * MOSH_FRAGMENT_SIZE,
                                          end = begin + packet.frag_len();
        std::copy(begin, end, frag->begin());

        glog.is(DEBUG1) && glog << packet.DebugString() << std::endl;

        fragments_.insert(packet);
    }
}

bool goby::apps::zeromq::acomms::Packetizer::add_fragment(const MoshPacket& frag)
{
    fragments_.insert(frag);

    // packet loss
    if (frag.is_last_frag() && fragments_.size() != frag.frag_num() + 1)
    {
        fragments_.clear();
        glog.is(WARN) && glog << "Missed fragment" << std::endl;
        return false;
    }

    return frag.is_last_frag();
}

std::vector<char> goby::apps::zeromq::acomms::Packetizer::reassemble()
{
    if (!fragments_.size())
        return std::vector<char>();

    std::vector<char> out(MOSH_FRAGMENT_SIZE * fragments_.size(), 0);

    std::vector<char>::iterator oit = out.begin();

    for (std::set<MoshPacket>::const_iterator it = fragments_.begin(), end = fragments_.end();
         it != end; ++it)
    {
        oit = std::copy(it->fragment().begin(), it->fragment().begin() + it->frag_len(), oit);
    }
    out.resize(out.size() - (MOSH_FRAGMENT_SIZE - fragments_.rbegin()->frag_len()));
    return out;
}
