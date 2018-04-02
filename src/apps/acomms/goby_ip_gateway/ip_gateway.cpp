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

#include <sys/socket.h>
#include <net/if.h>
#include <linux/if_tun.h>

#include <boost/bimap.hpp>
#include <boost/circular_buffer.hpp>

#include "dccl/arithmetic/field_codec_arithmetic.h"

#include "goby/pb/application.h"
#include "goby/util/binary.h"
#include "goby/acomms/ip_codecs.h"
#include "goby/acomms/protobuf/modem_message.pb.h"
#include "goby/acomms/acomms_constants.h"
#include "goby/acomms/amac.h"
#include "goby/acomms/connect.h"

#include "ip_gateway_config.pb.h"

enum { IPV4_ADDRESS_BITS = 32,
       MIN_IPV4_HEADER_LENGTH = 5, // number of 32-bit words
       UDP_HEADER_SIZE = 8,
       ICMP_HEADER_SIZE = 8,
       ICMP_TYPE = 250,
       IPV4_VERSION = 4,
       ICMP_CODE = 0};

int tun_alloc(char *dev);
int tun_config(const char* dev, const char* host, unsigned cidr_prefix, unsigned mtu);

using namespace goby::common::logger;

namespace goby
{
    namespace acomms
    {
        class IPGateway : public goby::pb::Application
        {
        public:
            IPGateway(goby::acomms::protobuf::IPGatewayConfig* cfg);
            ~IPGateway();
        private:
            void init_dccl();
            void init_tun();

            void loop();
            void receive_packets();

            void handle_udp_packet(const goby::acomms::protobuf::IPv4Header& ip_hdr,
                                   const goby::acomms::protobuf::UDPHeader& udp_hdr,
                                   const std::string& payload);
            void write_udp_packet(goby::acomms::protobuf::IPv4Header& ip_hdr,
                                  goby::acomms::protobuf::UDPHeader& udp_hdr,
                                  const std::string& payload);
            void write_icmp_control_message(const protobuf::IPGatewayICMPControl& control_msg);
            
            void write_icmp_packet(goby::acomms::protobuf::IPv4Header& ip_hdr,
                                   goby::acomms::protobuf::ICMPHeader& icmp_hdr,
                                   const std::string& payload);

            void icmp_report_queue();
            
            
            std::pair<int, int> to_src_dest_pair(int srcdest);
            int from_src_dest_pair(std::pair<int, int> src_dest);

            int ipv4_to_goby_address(const std::string& ipv4_address);
            std::string goby_address_to_ipv4(int modem_id);
            
            void handle_data_request(const protobuf::ModemTransmission& m);
            void handle_initiate_transmission(const protobuf::ModemTransmission& m);
            void handle_modem_receive(const goby::acomms::protobuf::ModemTransmission& m);

            int ac_freq(int srcdest);
             
        private:
            goby::acomms::protobuf::IPGatewayConfig& cfg_;
            dccl::Codec dccl_goby_nh_, dccl_ip_, dccl_udp_, dccl_icmp_;
            int tun_fd_;
            int total_addresses_;
            std::uint32_t local_address_; // in host byte order
            int local_modem_id_;
            std::uint32_t netmask_; // in host byte order

            goby::acomms::MACManager mac_;

            // map goby_port to udp_port
            boost::bimap<int, int> port_map_;
            int dynamic_port_index_;
            std::vector<int> dynamic_udp_fd_;

            int ip_mtu_; // the MTU on the tun interface, which is slightly different than the Goby MTU specified in the config file since the IP and Goby NetworkHeader are different sizes.

            // maps destination goby address to message buffer
            std::map<int, boost::circular_buffer<std::string> > outgoing_;
        };        
    }
}

goby::acomms::IPGateway::IPGateway(goby::acomms::protobuf::IPGatewayConfig* cfg)
    : Application(cfg),
      cfg_(*cfg),
      dccl_goby_nh_("ip_gateway_id_codec_0"),
      dccl_ip_("ip_gateway_id_codec_1"),
      dccl_udp_("ip_gateway_id_codec_2"),
      dccl_icmp_("ip_gateway_id_codec_3"),
      tun_fd_(-1),
      total_addresses_((1 << (IPV4_ADDRESS_BITS-cfg_.cidr_netmask_prefix()))-1), // minus one since we don't need to use .255 as broadcast
      local_address_(0),
      local_modem_id_(0),
      netmask_(0),
      dynamic_port_index_(cfg_.static_udp_port_size())
{

    for(int d = 0; d < total_addresses_; ++d)
    {
        for(int s = 0; s < total_addresses_; ++s)
        {
            int S = from_src_dest_pair(std::make_pair(s,d));
            std::cout << std::setw(8) << S;
            if(s != 0 && s != d)
            {
                std::pair<int, int> sd = to_src_dest_pair(S);
                std::cout << "(" << sd.first << "," << sd.second << ")";
                assert(sd.first == s && sd.second == d);
            }
            else
                std::cout << "(" << s << "," << d << ")";

        }

        std::cout << std::endl;
    }
    
    
    init_dccl();
    init_tun();

    Application::subscribe(&IPGateway::handle_data_request, this, "DataRequest" + goby::util::as<std::string>(local_modem_id_));
    Application::subscribe(&IPGateway::handle_modem_receive, this, "Rx" + goby::util::as<std::string>(local_modem_id_));

    goby::acomms::connect(&mac_.signal_initiate_transmission, this, &IPGateway::handle_initiate_transmission);
    cfg_.mutable_mac_cfg()->set_modem_id(local_modem_id_);
    mac_.startup(cfg_.mac_cfg());
    
}
void goby::acomms::IPGateway::init_dccl()
{
    if(cfg_.model_type() == protobuf::IPGatewayConfig::AUTONOMY_COLLABORATION)
    {
        if(cfg_.gamma_autonomy() > 1 || cfg_.gamma_autonomy() < 0)
            glog.is(DIE) && glog << "gamma_autonomy must be [0, 1]" << std::endl;
        
        if(cfg_.gamma_collaboration() > cfg_.gamma_autonomy() || cfg_.gamma_collaboration() < 0)
            glog.is(DIE) && glog << "gamma_collaboration must be [0, gamma_autonomy]" << std::endl;
    }
    
    
    dccl::dlog.connect(dccl::logger::INFO, &std::cout);
    
    dccl_arithmetic_load(&dccl_goby_nh_);
    
    dccl::arith::protobuf::ArithmeticModel addr_model;
    addr_model.set_name("goby.acomms.NetworkHeader.AddrModel");
    
    for(int i = 0, n = (total_addresses_-1)*(total_addresses_-1); i <= n; ++i)
    {
        if(i != n)
        {
            int freq = 10;
            switch(cfg_.model_type())
            {
                case protobuf::IPGatewayConfig::UNIFORM:
                    // already set to uniform value
                    break;
                case protobuf::IPGatewayConfig::AUTONOMY_COLLABORATION:
                    freq = ac_freq(i);
                    break;
            }
            
            addr_model.add_value_bound(i);
            addr_model.add_frequency(freq);
        }
        else
        {
            addr_model.add_value_bound(n);
        }
    }

    addr_model.set_eof_frequency(0); 
    addr_model.set_out_of_range_frequency(0);
    glog.is(DEBUG1) && glog << addr_model.DebugString() << std::endl;
    
    dccl::arith::ModelManager::set_model(addr_model);        

    if(cfg_.total_ports() < cfg_.static_udp_port_size())
        glog.is(DIE) && glog << "total_ports must be at least as many as the static_udp_ports defined" << std::endl;

    for(int i = 0, n = cfg_.total_ports(); i < n; ++i)
    {
        if(i < cfg_.static_udp_port_size())
            port_map_.insert(boost::bimap<int, int>::value_type(i, cfg_.static_udp_port(i)));
        else
            port_map_.insert(boost::bimap<int, int>::value_type(i, -i));
    }
    
    dccl::arith::protobuf::ArithmeticModel port_model;
    port_model.set_name("goby.acomms.NetworkHeader.PortModel");
    for(int i = 0, n = cfg_.total_ports(); i <= n; ++i)
    {
        if(i != n)
        {
            int freq = 10;
            port_model.add_value_bound(i);
            port_model.add_frequency(freq);
        }
        else
        {
            port_model.add_value_bound(n);
        }
    }
    port_model.set_eof_frequency(0);
    port_model.set_out_of_range_frequency(0);
    dccl::arith::ModelManager::set_model(port_model);

    dccl_goby_nh_.load<goby::acomms::protobuf::NetworkHeader>();
    dccl_ip_.load<goby::acomms::protobuf::IPv4Header>();
    dccl_udp_.load<goby::acomms::protobuf::UDPHeader>();
    dccl_icmp_.load<goby::acomms::protobuf::ICMPHeader>(); 
   
    dccl_goby_nh_.info_all(&std::cout);
}

void goby::acomms::IPGateway::init_tun()
{
    char tun_name[IFNAMSIZ];

    std::string desired_tun_name = "tun";
    if(cfg_.has_tun_number())
        desired_tun_name += goby::util::as<std::string>(cfg_.tun_number());
    else
        desired_tun_name += "%d";
    
    strcpy(tun_name, desired_tun_name.c_str());
    tun_fd_ = tun_alloc(tun_name);    
    if(tun_fd_ < 0)
        glog.is(DIE) && glog << "Could not allocate tun interface. Check permissions?" << std::endl;

    ip_mtu_ = cfg_.mtu()-dccl_goby_nh_.max_size<goby::acomms::protobuf::NetworkHeader>()+MIN_IPV4_HEADER_LENGTH*4;

    int ret = tun_config(tun_name, cfg_.local_ipv4_address().c_str(), cfg_.cidr_netmask_prefix(), ip_mtu_);
    if(ret < 0)
        glog.is(DIE) && glog << "Could not configure tun interface. Check IP address: " << cfg_.local_ipv4_address() << " and netmask prefix: " << cfg_.cidr_netmask_prefix() << std::endl;
    
    in_addr local_addr;
    inet_aton(cfg_.local_ipv4_address().c_str(), &local_addr);
    local_address_ = ntohl(local_addr.s_addr);    
    netmask_ = 0xFFFFFFFF - ((1 << (IPV4_ADDRESS_BITS-cfg_.cidr_netmask_prefix()))-1);
    local_modem_id_ = ipv4_to_goby_address(cfg_.local_ipv4_address());
}


goby::acomms::IPGateway::~IPGateway()
{
    dccl_arithmetic_unload(&dccl_goby_nh_);
}



void goby::acomms::IPGateway::loop()
{
    mac_.do_work();
    receive_packets();
}



void goby::acomms::IPGateway::handle_udp_packet(
    const goby::acomms::protobuf::IPv4Header& ip_hdr,
    const goby::acomms::protobuf::UDPHeader& udp_hdr,
    const std::string& payload)
{
    
    glog.is(VERBOSE) && glog << "Received UDP Packet. IPv4 Header: " << ip_hdr.DebugString() << "UDP Header: " << udp_hdr <<  "Payload (" << payload.size() << " bytes): " << goby::util::hex_encode(payload) << std::endl;

    int src = ipv4_to_goby_address(ip_hdr.source_ip_address());
    int dest = ipv4_to_goby_address(ip_hdr.dest_ip_address());
    
    goby::acomms::protobuf::NetworkHeader net_header;
    net_header.set_protocol(goby::acomms::protobuf::NetworkHeader::UDP);
    net_header.set_srcdest_addr(from_src_dest_pair(std::make_pair(src,dest)));

    // map destination first - we need this mapping to exist on the other end
    // if we map the source first, we might use the source mapping when source port == dest port
    int dest_port = 0, src_port = 0;
    boost::bimap<int, int>::right_map::const_iterator dest_it = port_map_.right.find(udp_hdr.dest_port());
    if(dest_it != port_map_.right.end())
    {
        dest_port = dest_it->second;
    }
    else
    {
        glog.is(WARN) && glog << "No mapping for destination UDP port: " << udp_hdr.dest_port() << ". Unable to send packet." << std::endl;
        return;
    }

    
    boost::bimap<int, int>::right_map::const_iterator src_it = port_map_.right.find(udp_hdr.source_port());
    if(src_it != port_map_.right.end())
    {
        src_port = src_it->second;
    }
    else
    {
        // on transmit, try to map the source port to a dynamic port
        if(cfg_.total_ports() == cfg_.static_udp_port_size())
        {
            glog.is(WARN) && glog << "No mapping for source UDP port: " << udp_hdr.source_port() << " and we have no dynamic ports allocated (static_udp_port size == total_ports)" << std::endl;
            return;
        }
        else
        {
            boost::bimap<int, int>::left_map::iterator dyn_port_it = port_map_.left.find(dynamic_port_index_);
            port_map_.left.replace_data(dyn_port_it, udp_hdr.source_port());
            net_header.mutable_udp()->add_srcdest_port(dynamic_port_index_);
            ++dynamic_port_index_;
            if(dynamic_port_index_ >= cfg_.total_ports())
                dynamic_port_index_ = cfg_.static_udp_port_size();
        }
    }

    net_header.mutable_udp()->add_srcdest_port(src_port);
    net_header.mutable_udp()->add_srcdest_port(dest_port);    
    
    glog.is(VERBOSE) && glog << "NetHeader: " << net_header.DebugString() << std::endl;
    
    std::string nh;
    dccl_goby_nh_.encode(&nh, net_header);

    std::map<int, boost::circular_buffer<std::string> >::iterator it = outgoing_.find(dest);
    if(it == outgoing_.end())
    {
        std::pair<std::map<int, boost::circular_buffer<std::string> >::iterator, bool> itboolpair =
            outgoing_.insert(std::make_pair(dest, boost::circular_buffer<std::string>(cfg_.queue_size())));
        it = itboolpair.first;
    }
    
    it->second.push_back(nh + payload);
    icmp_report_queue();
}

void goby::acomms::IPGateway::handle_initiate_transmission(const protobuf::ModemTransmission& m)
{
    publish(m, "Tx" + goby::util::as<std::string>(local_modem_id_));
}

void goby::acomms::IPGateway::receive_packets()
{
    while(true)
    {
        fd_set rd_set;
        FD_ZERO(&rd_set);
        FD_SET(tun_fd_, &rd_set);

        timeval tout = {0};
        int ret = select(tun_fd_+1, &rd_set, 0, 0, &tout);

        if (ret < 0 && errno != EINTR) {
            glog.is(WARN) && glog << "Could not select on tun fd." << std::endl;
            return;
        }
        else if(ret > 0)
        {
            char buffer[ip_mtu_+1];
            int len = read(tun_fd_, buffer, ip_mtu_);

            if ( len < 0 )
            {
                glog.is(WARN) && glog << "tun read error." << std::endl;
            }
            else if ( len == 0 )
            {
                glog.is(DIE) && glog << "tun reached EOF." << std::endl;
            }
            else
            {
                goby::acomms::protobuf::IPv4Header ip_hdr;
                unsigned short ip_header_size = (buffer[0] & 0xF) * 4;
                unsigned short version = ((buffer[0] >> 4) & 0xF);
                if(version == 4)
                {
                    std::string header_data(buffer, ip_header_size);
                    dccl_ip_.decode(header_data, &ip_hdr);
                    glog.is(DEBUG2) && glog << "Received " << len << " bytes. " << std::endl;
                    switch(ip_hdr.protocol())
                    {
                        default:
                            glog.is(DEBUG1) && glog << "IPv4 Protocol " << ip_hdr.protocol() << " is not supported." << std::endl;
                            break;
                        case IPPROTO_UDP:
                        {
                            goby::acomms::protobuf::UDPHeader udp_hdr;
                            std::string udp_header_data(&buffer[ip_header_size], UDP_HEADER_SIZE); 
                            dccl_udp_.decode(udp_header_data, &udp_hdr);
                            handle_udp_packet(ip_hdr, udp_hdr, std::string(&buffer[ip_header_size+UDP_HEADER_SIZE], ip_hdr.total_length()-ip_header_size-UDP_HEADER_SIZE));
                            break;
                        }
                        case IPPROTO_ICMP:
                        {
                            goby::acomms::protobuf::ICMPHeader icmp_hdr;
                            std::string icmp_header_data(&buffer[ip_header_size], ICMP_HEADER_SIZE);
                            dccl_icmp_.decode(icmp_header_data, &icmp_hdr);
                            glog.is(DEBUG1) && glog << "Received ICMP Packet with header: " << icmp_hdr.ShortDebugString() << std::endl;
                            glog.is(DEBUG1) && glog << "ICMP sending is not supported." << std::endl;

                            break;
                        }
                        
                    }
                }
            
            }
        }
        else
        {
            // no select items
            return;
        }
    }
}



void goby::acomms::IPGateway::handle_data_request(const protobuf::ModemTransmission& orig_msg)
{
    if(cfg_.has_only_rate() && cfg_.only_rate() != orig_msg.rate())
        return;
    
    protobuf::ModemTransmission msg = orig_msg;

    bool had_data = false;
    while((unsigned)msg.frame_size() < msg.max_num_frames())
    {
        if(msg.dest() != goby::acomms::QUERY_DESTINATION_ID)
        {
            std::map<int, boost::circular_buffer<std::string> >::iterator it = outgoing_.find(msg.dest());
            if(it == outgoing_.end())
            {
                break;
            }
            else if(it->second.size() == 0)
            {
                break;
            }
            else
            {
                msg.set_ack_requested(false);
                msg.add_frame(it->second.front());
                it->second.pop_front();
                had_data = true;
            }
        }
        else
        {
            // TODO: not fair - prioritizes lower valued destinations
            for(std::map<int, boost::circular_buffer<std::string> >::iterator it = outgoing_.begin(),
                    end = outgoing_.end(); it != end; ++it)
            {
                if(it->second.size() == 0)
                {
                    continue;
                }
                else
                {
                    msg.set_dest(it->first);
                    msg.set_ack_requested(false);
                    msg.add_frame(it->second.front());
                    it->second.pop_front();
                    had_data = true;
                    break;
                }
            }
            break;
        }
        
    }
    
    publish(msg, "DataResponse" + goby::util::as<std::string>(local_modem_id_));
    if(had_data)
        icmp_report_queue();
}

void goby::acomms::IPGateway::handle_modem_receive(const goby::acomms::protobuf::ModemTransmission& modem_msg)
{
    if(cfg_.has_only_rate() && cfg_.only_rate() != modem_msg.rate())
        return;
    
    for(int i = 0, n = modem_msg.frame_size(); i < n; ++i)
    {
        goby::acomms::protobuf::IPv4Header ip_hdr;
        goby::acomms::protobuf::UDPHeader udp_hdr;

        goby::acomms::protobuf::NetworkHeader net_header;
        std::string frame = modem_msg.frame(i);

        try {
            dccl_goby_nh_.decode(&frame, &net_header); // strips used bytes off frame
        }
        catch (goby::Exception &e) {
            glog.is(WARN) && glog << "Could not decode header: " << e.what() << std::endl;
            continue;
        }        
            
        glog.is(VERBOSE) && glog << "NetHeader: " << net_header.DebugString() << std::endl;
           
        ip_hdr.set_ihl(MIN_IPV4_HEADER_LENGTH);
        ip_hdr.set_version(IPV4_VERSION);
        ip_hdr.set_ecn(0);
        ip_hdr.set_dscp(0);
        ip_hdr.set_total_length(MIN_IPV4_HEADER_LENGTH*4 + UDP_HEADER_SIZE + frame.size());
        ip_hdr.set_identification(0);
        ip_hdr.mutable_flags_frag_offset()->set_dont_fragment(false); 
        ip_hdr.mutable_flags_frag_offset()->set_more_fragments(false);
        ip_hdr.mutable_flags_frag_offset()->set_fragment_offset(0);
        ip_hdr.set_ttl(63);
        ip_hdr.set_protocol(net_header.protocol());

        std::pair<int, int> src_dest = to_src_dest_pair(net_header.srcdest_addr());
        ip_hdr.set_source_ip_address(goby_address_to_ipv4(src_dest.first));
        ip_hdr.set_dest_ip_address(goby_address_to_ipv4(src_dest.second));
        
        if(net_header.udp().srcdest_port_size() == 2)
        {
            boost::bimap<int, int>::left_map::iterator src_it = port_map_.left.find(net_header.udp().srcdest_port(0));
            int source_port = src_it->second;
            if(source_port < 0)
            {
                // get an unused UDP port
                int fd = socket(AF_INET, SOCK_DGRAM, 0);
                struct sockaddr_in addr;
                memset((char *)&addr, 0, sizeof(addr));
                addr.sin_family = AF_INET;
                addr.sin_addr.s_addr = htonl(INADDR_ANY);
                addr.sin_port = htons(0);
                bind(fd, (struct sockaddr *)&addr, sizeof(addr));
                
                struct sockaddr_in sa;
                socklen_t sa_len;
                sa_len = sizeof(sa);
                getsockname(fd, (struct sockaddr *)&sa, &sa_len);
                source_port = ntohs(sa.sin_port);
                dynamic_udp_fd_.push_back(fd);

                port_map_.left.replace_data(src_it, source_port);
            }
            
            
            boost::bimap<int, int>::left_map::const_iterator dest_it = port_map_.left.find(net_header.udp().srcdest_port(1));
            int dest_port = dest_it->second;
            if(dest_port < 0)
            {
                glog.is(WARN) && glog << "No mapping for destination port: " << net_header.udp().srcdest_port(1) << ", cannot write packet" << std::endl;
                continue;
            }
            
            udp_hdr.set_source_port(source_port);
            udp_hdr.set_dest_port(dest_port);
        }
        else
        {
            glog.is(WARN) && glog << "Bad srcdest_port field, must have two values." << std::endl;
            continue;
        }

        udp_hdr.set_length(UDP_HEADER_SIZE + frame.size());
        write_udp_packet(ip_hdr, udp_hdr, frame);
    }
}



void goby::acomms::IPGateway::write_udp_packet(goby::acomms::protobuf::IPv4Header& ip_hdr, goby::acomms::protobuf::UDPHeader& udp_hdr, const std::string& payload)
{
    // set checksum 0 for calculation
    ip_hdr.set_header_checksum(0);
    udp_hdr.set_checksum(0);
    
    std::string ip_hdr_data, udp_hdr_data;
    dccl_udp_.encode(&udp_hdr_data, udp_hdr);
    dccl_ip_.encode(&ip_hdr_data, ip_hdr);

    enum { NET_SHORT_BYTES = 2, NET_LONG_BYTES = 4 };
    enum { IPV4_SOURCE_ADDR_OFFSET = 12, IPV4_DEST_ADDR_OFFSET = 16, IPV4_CS_OFFSET = 10 };
    enum { UDP_LENGTH_OFFSET = 4, UDP_CS_OFFSET = 6};
    
    std::string udp_pseudo_header = ip_hdr_data.substr(IPV4_SOURCE_ADDR_OFFSET, NET_LONG_BYTES) + ip_hdr_data.substr(IPV4_DEST_ADDR_OFFSET, NET_LONG_BYTES) + char(0) + char(IPPROTO_UDP) + udp_hdr_data.substr(UDP_LENGTH_OFFSET, NET_SHORT_BYTES);
    assert(udp_pseudo_header.size() == 12);

    uint16_t ip_checksum = net_checksum(ip_hdr_data);
    uint16_t udp_checksum = net_checksum(udp_pseudo_header + udp_hdr_data + payload);

    ip_hdr_data[IPV4_CS_OFFSET] = (ip_checksum >> 8) & 0xFF;
    ip_hdr_data[IPV4_CS_OFFSET+1] = ip_checksum & 0xFF;

    udp_hdr_data[UDP_CS_OFFSET] = (udp_checksum >> 8) & 0xFF;
    udp_hdr_data[UDP_CS_OFFSET+1] = udp_checksum & 0xFF;

    
    std::string packet(ip_hdr_data + udp_hdr_data + payload);
    unsigned len = write(tun_fd_, packet.c_str(), packet.size());
    if(len < packet.size())
        glog.is(WARN) && glog << "Failed to write all " << packet.size() << " bytes." << std::endl;
}

void goby::acomms::IPGateway::icmp_report_queue()
{
    protobuf::IPGatewayICMPControl control_msg;
    control_msg.set_type(protobuf::IPGatewayICMPControl::QUEUE_REPORT);
    control_msg.set_address(cfg_.local_ipv4_address());

    int total_messages = 0;
    
    for(std::map<int, boost::circular_buffer<std::string> >::const_iterator it = outgoing_.begin(), end = outgoing_.end(); it != end; ++it)
    {
        int size = it->second.size();
        if(size)
        {
            protobuf::IPGatewayICMPControl::QueueReport::SubQueue* q = control_msg.mutable_queue_report()->add_queue();
            q->set_dest(it->first);
            q->set_size(size);
            total_messages += size;
        }
    }
    write_icmp_control_message(control_msg);

    // skip the MACManager and directly initiate the transmission
    if(total_messages > 0 && cfg_.bypass_mac())
    {
        if(cfg_.has_bypass_mac_slot())
        {
            handle_initiate_transmission(cfg_.bypass_mac_slot());
        }
        else
        {
            protobuf::ModemTransmission m;
            m.set_src(local_modem_id_);
            m.set_type(protobuf::ModemTransmission::DATA);
            if(cfg_.has_only_rate())
                m.set_rate(cfg_.only_rate());
            handle_initiate_transmission(m);
        }
    }    
}

void goby::acomms::IPGateway::write_icmp_control_message(const protobuf::IPGatewayICMPControl& control_msg)
{
    std::string control_data;
    control_msg.SerializeToString(&control_data);

    glog.is(DEBUG1) && glog << "Writing ICMP Control message: " << control_msg.DebugString() << std::endl;
    
    
    goby::acomms::protobuf::IPv4Header ip_hdr;
    ip_hdr.set_ihl(MIN_IPV4_HEADER_LENGTH);
    ip_hdr.set_version(IPV4_VERSION);
    ip_hdr.set_ecn(0);
    ip_hdr.set_dscp(0);
    ip_hdr.set_total_length(MIN_IPV4_HEADER_LENGTH*4 + ICMP_HEADER_SIZE + control_data.size());
    ip_hdr.set_identification(0);
    ip_hdr.mutable_flags_frag_offset()->set_dont_fragment(false); 
    ip_hdr.mutable_flags_frag_offset()->set_more_fragments(false);
    ip_hdr.mutable_flags_frag_offset()->set_fragment_offset(0);
    ip_hdr.set_ttl(63);
    ip_hdr.set_protocol(IPPROTO_ICMP);
    ip_hdr.set_source_ip_address(cfg_.local_ipv4_address());
    ip_hdr.set_dest_ip_address(cfg_.local_ipv4_address());

    goby::acomms::protobuf::ICMPHeader icmp_hdr;
    icmp_hdr.set_type(ICMP_TYPE);
    icmp_hdr.set_code(ICMP_CODE);
    
    write_icmp_packet(ip_hdr, icmp_hdr, control_data);
}



void goby::acomms::IPGateway::write_icmp_packet(goby::acomms::protobuf::IPv4Header& ip_hdr, goby::acomms::protobuf::ICMPHeader& icmp_hdr, const std::string& payload)
{
    // set checksum 0 for calculation
    ip_hdr.set_header_checksum(0);
    icmp_hdr.set_checksum(0);
    icmp_hdr.set_short1(0);
    icmp_hdr.set_short2(0);
    
    std::string ip_hdr_data, icmp_hdr_data;
    dccl_icmp_.encode(&icmp_hdr_data, icmp_hdr);
    dccl_ip_.encode(&ip_hdr_data, ip_hdr);

    enum { NET_SHORT_BYTES = 2, NET_LONG_BYTES = 4 };
    enum { IPV4_SOURCE_ADDR_OFFSET = 12, IPV4_DEST_ADDR_OFFSET = 16, IPV4_CS_OFFSET = 10 };
    enum { ICMP_CS_OFFSET = 2};

    uint16_t ip_checksum = net_checksum(ip_hdr_data);
    uint16_t icmp_checksum = net_checksum(icmp_hdr_data + payload);

    ip_hdr_data[IPV4_CS_OFFSET] = (ip_checksum >> 8) & 0xFF;
    ip_hdr_data[IPV4_CS_OFFSET+1] = ip_checksum & 0xFF;

    icmp_hdr_data[ICMP_CS_OFFSET] = (icmp_checksum >> 8) & 0xFF;
    icmp_hdr_data[ICMP_CS_OFFSET+1] = icmp_checksum & 0xFF;
    
    std::string packet(ip_hdr_data + icmp_hdr_data + payload);
    unsigned len = write(tun_fd_, packet.c_str(), packet.size());
    if(len < packet.size())
        glog.is(WARN) && glog << "Failed to write all " << packet.size() << " bytes." << std::endl;
}

//          src
//       0  1  2  3
//     ------------
// d 0 | x  0  1  2 
// e 1 | x  x  3  4  
// s 2 | x  5  x  6  
// t 3 | x  7  8  x 
std::pair<int, int> goby::acomms::IPGateway::to_src_dest_pair(int srcdest)
{
    int src = (srcdest-1) % (total_addresses_-2) + 1;
    int dest = (srcdest-1) / (total_addresses_-2);
    if(src >= dest)
        ++src;

    return std::make_pair(src, dest);
}

int goby::acomms::IPGateway::from_src_dest_pair(std::pair<int, int> src_dest)
{
    int src = src_dest.first;
    int dest = src_dest.second;

    if(src == dest || src == goby::acomms::BROADCAST_ID) return -1;

    return dest * (total_addresses_-2) + src - (src > dest ? 1 : 0);
}


int goby::acomms::IPGateway::ipv4_to_goby_address(const std::string& ipv4_address)
{
    in_addr remote_addr;
    inet_aton(ipv4_address.c_str(), &remote_addr);
    std::uint32_t remote_address = ntohl(remote_addr.s_addr);
    int modem_id = remote_address & ~netmask_;

    // broadcast conventions differ
    if(modem_id + netmask_ == 0xFFFFFFFF)
        modem_id = goby::acomms::BROADCAST_ID;
    return modem_id;
}

std::string goby::acomms::IPGateway::goby_address_to_ipv4(int modem_id)
{
    std::uint32_t address = 0;
    if(modem_id == goby::acomms::BROADCAST_ID)
        address = (local_address_ & netmask_) + ~netmask_;
    else
        address = (local_address_ & netmask_) + modem_id;
    
    in_addr ret_addr;
    ret_addr.s_addr = htonl(address);
    return std::string(inet_ntoa(ret_addr));
}

int goby::acomms::IPGateway::ac_freq(int srcdest)
{
    std::pair<int, int> sd = to_src_dest_pair(srcdest);
    int s = sd.first;
    int d = sd.second;    
    int N = total_addresses_ + 1;
    const int scale_factor = N*N*100;
    
    double p0 = 1.0/(N-2);
    double p1 = 1.0/(N-3);
    double p2 = 1.0/(N*N-6*N+9);

    if(s == d || s == goby::acomms::BROADCAST_ID)
        return 0;
    else if(s == cfg_.gateway_id())
        return scale_factor*p0*(1-cfg_.gamma_autonomy());
    else if(d == cfg_.gateway_id())
        return scale_factor*p1*(cfg_.gamma_autonomy() - cfg_.gamma_collaboration());
    else
        return scale_factor*p2*(cfg_.gamma_collaboration());
}




int main(int argc, char* argv[])
{
    dccl::FieldCodecManager::add<goby::acomms::IPGatewayEmptyIdentifierCodec<0xF000> >("ip_gateway_id_codec_0");
    dccl::FieldCodecManager::add<goby::acomms::IPGatewayEmptyIdentifierCodec<0xF001> >("ip_gateway_id_codec_1");
    dccl::FieldCodecManager::add<goby::acomms::IPGatewayEmptyIdentifierCodec<0xF002> >("ip_gateway_id_codec_2");
    dccl::FieldCodecManager::add<goby::acomms::IPGatewayEmptyIdentifierCodec<0xF003> >("ip_gateway_id_codec_3");
    dccl::FieldCodecManager::add<goby::acomms::NetShortCodec>("net.short");
    dccl::FieldCodecManager::add<goby::acomms::IPv4AddressCodec>("ip.v4.address");
    dccl::FieldCodecManager::add<goby::acomms::IPv4FlagsFragOffsetCodec>("ip.v4.flagsfragoffset");
    
    goby::acomms::protobuf::IPGatewayConfig cfg;
    return goby::run<goby::acomms::IPGateway>(argc, argv, &cfg);
}
                            
    

// https://www.kernel.org/doc/Documentation/networking/tuntap.txt
//  char *dev should be the name of the device with a format string (e.g.
//  "tun%d"), but (as far as I can see) this can be any valid network device name.
//  Note that the character pointer becomes overwritten with the real device name
//  (e.g. "tun0")
int tun_alloc(char *dev)
{
    struct ifreq ifr;
    int fd, err;

    if( (fd = open("/dev/net/tun", O_RDWR)) < 0 )
        return fd;

    memset(&ifr, 0, sizeof(ifr));

    /* Flags: IFF_TUN   - TUN device (no Ethernet headers) 
     *        IFF_TAP   - TAP device  
     *
     *        IFF_NO_PI - Do not provide packet information  
     */ 
    ifr.ifr_flags = IFF_TUN | IFF_NO_PI; 
    if(*dev)
        strncpy(ifr.ifr_name, dev, IFNAMSIZ);

    if((err = ioctl(fd, TUNSETIFF, (void *) &ifr)) < 0)
    {
        close(fd);
        return err;
    }

    
    strcpy(dev, ifr.ifr_name);
    return fd;
}              

// from https://stackoverflow.com/questions/5308090/set-ip-address-using-siocsifaddr-ioctl
int tun_config(const char* dev, const char* host, unsigned cidr_prefix, unsigned mtu)
{
    // set address 

    struct ifreq ifr;
    struct sockaddr_in* sai = (struct sockaddr_in *)&ifr.ifr_addr;
    int sockfd;
        
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);

    memset(&ifr, 0, sizeof(ifr));

    strncpy(ifr.ifr_name, dev, IFNAMSIZ);


    sai->sin_family = AF_INET;
    sai->sin_port = 0;
        
    int ret = inet_aton(host, &sai->sin_addr);
    if(ret == 0)
        return -1;

    if(ioctl(sockfd, SIOCSIFADDR, &ifr) == -1)
        return -1;
        
    if(ioctl(sockfd, SIOCGIFFLAGS, &ifr) == -1)
        return -1;

    ifr.ifr_mtu = mtu;
    
    if(ioctl(sockfd, SIOCSIFMTU, &ifr) == -1)
        return -1;
        
    ifr.ifr_flags |= IFF_UP | IFF_RUNNING;

    if(ioctl(sockfd, SIOCSIFFLAGS, &ifr) == -1)
        return -1;


    // set mask
        
    struct ifreq ifr_mask;
    struct sockaddr_in* sai_mask = (struct sockaddr_in *)&ifr_mask.ifr_addr;
    memset(&ifr_mask, 0, sizeof(ifr_mask));
    strncpy(ifr_mask.ifr_name, dev, IFNAMSIZ);

    sai_mask->sin_family = AF_INET;
    sai_mask->sin_port = 0;

    if(cidr_prefix > IPV4_ADDRESS_BITS)
        return -1;
        
    sai_mask->sin_addr.s_addr = htonl(0xFFFFFFFF - ((1 << (IPV4_ADDRESS_BITS-cidr_prefix))-1));

    if(ioctl(sockfd, SIOCSIFNETMASK, &ifr_mask) == -1)
        return -1;
        
    close(sockfd);
    
    return 0;    
}

