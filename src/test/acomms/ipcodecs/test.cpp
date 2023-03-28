// Copyright 2016-2021:
//   GobySoft, LLC (2013-)
//   Community contributors (see AUTHORS file)
// File authors:
//   Toby Schneider <toby@gobysoft.org>
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

#include <arpa/inet.h>
#include <dccl/codec.h>
#include <netinet/in.h>

#include "goby/acomms/ip_codecs.h"
#include "goby/util/binary.h"
#include "goby/util/dccl_compat.h"
#include "goby/util/debug_logger.h"

using namespace goby::util::logger;
using goby::glog;

void ip_test(const std::string& hex, dccl::Codec& dccl_1)
{
    goby::acomms::protobuf::IPv4Header iphdr;
    std::string header_data(goby::util::hex_decode(hex));
    dccl_1.decode(header_data, &iphdr);
    glog.is(VERBOSE) && glog << "Header is: " << goby::util::hex_encode(header_data) << "\n"
                             << iphdr.DebugString() << std::endl;

    std::string test;
    dccl_1.encode(&test, iphdr);
    glog.is(VERBOSE) && glog << "Re-encoded as: " << goby::util::hex_encode(test) << std::endl;
    assert(test == header_data);

    unsigned orig_cs = iphdr.header_checksum();
    iphdr.set_header_checksum(0);
    dccl_1.encode(&test, iphdr);

    assert(goby::acomms::net_checksum(header_data) == 0);
    assert(goby::acomms::net_checksum(test) == orig_cs);
}

int main(int /*argc*/, char* argv[])
{
    goby::glog.add_stream(goby::util::logger::DEBUG3, &std::cerr);
    goby::glog.set_name(argv[0]);

#ifdef DCCL_VERSION_4_1_OR_NEWER
    dccl::Codec dccl_1("ip_gateway_id_codec_1",
                       goby::acomms::IPGatewayEmptyIdentifierCodec<0xF001>());
    dccl_1.manager().add<goby::acomms::NetShortCodec>("net.short");
    dccl_1.manager().add<goby::acomms::IPv4AddressCodec>("ip.v4.address");
    dccl_1.manager().add<goby::acomms::IPv4FlagsFragOffsetCodec>("ip.v4.flagsfragoffset");
#else
    dccl::FieldCodecManager::add<goby::acomms::IPGatewayEmptyIdentifierCodec<0xF001>>(
        "ip_gateway_id_codec_1");
    dccl::FieldCodecManager::add<goby::acomms::NetShortCodec>("net.short");
    dccl::FieldCodecManager::add<goby::acomms::IPv4AddressCodec>("ip.v4.address");
    dccl::FieldCodecManager::add<goby::acomms::IPv4FlagsFragOffsetCodec>("ip.v4.flagsfragoffset");

    dccl::Codec dccl_1("ip_gateway_id_codec_1");
#endif
    dccl_1.load<goby::acomms::protobuf::IPv4Header>();

    // ICMP Ping
    ip_test("4500001c837e40004001360dc0a80001c0a80004", dccl_1);
    // UDP
    ip_test("4500003e2fe8400040116d42c0a88e32c0a88e01", dccl_1);
    // UDP
    ip_test("4500004cb803000038111b4d40712005c0a88e32", dccl_1);

    std::cout << "all tests passed" << std::endl;
}
