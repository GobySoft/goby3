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

// tests all protobuf types with _default codecs, repeat and non repeat

#include <google/protobuf/descriptor.pb.h>
#include <boost/foreach.hpp>

#include "goby/acomms/dccl.h"
#include "test.pb.h"
#include "goby/util/as.h"
#include "goby/common/time.h"
#include "goby/util/binary.h"

void decode_check(const std::string& encoded);
using goby::acomms::operator<<;
goby::acomms::DCCLCodec* codec;
TestMsg msg_in;

int main(int argc, char* argv[])
{
    goby::glog.add_stream(goby::common::logger::DEBUG3, &std::cerr);
    goby::glog.set_name(argv[0]);
    
    goby::acomms::protobuf::DCCLConfig cfg;
    codec = goby::acomms::DCCLCodec::get();
    codec->set_cfg(cfg);

    int i = 0;
    msg_in.set_double_default_optional(++i + 0.1);
    msg_in.set_float_default_optional(++i + 0.2);

    msg_in.set_int32_default_optional(++i);
    msg_in.set_int64_default_optional(-++i);
    msg_in.set_uint32_default_optional(++i);
    msg_in.set_uint64_default_optional(++i);
    msg_in.set_sint32_default_optional(-++i);
    msg_in.set_sint64_default_optional(++i);
    msg_in.set_fixed32_default_optional(++i);
    msg_in.set_fixed64_default_optional(++i);
    msg_in.set_sfixed32_default_optional(++i);
    msg_in.set_sfixed64_default_optional(-++i);

    msg_in.set_bool_default_optional(true);

    msg_in.set_string_default_optional("abc123");
    msg_in.set_bytes_default_optional(goby::util::hex_decode("00112233aabbcc1234"));
    
    msg_in.set_enum_default_optional(ENUM_C);
    msg_in.mutable_msg_default_optional()->set_val(++i + 0.3);
    msg_in.mutable_msg_default_optional()->mutable_msg()->set_val(++i);

    msg_in.set_double_default_required(++i + 0.1);
    msg_in.set_float_default_required(++i + 0.2);

    msg_in.set_int32_default_required(++i);
    msg_in.set_int64_default_required(-++i);
    msg_in.set_uint32_default_required(++i);
    msg_in.set_uint64_default_required(++i);
    msg_in.set_sint32_default_required(-++i);
    msg_in.set_sint64_default_required(++i);
    msg_in.set_fixed32_default_required(++i);
    msg_in.set_fixed64_default_required(++i);
    msg_in.set_sfixed32_default_required(++i);
    msg_in.set_sfixed64_default_required(-++i);

    msg_in.set_bool_default_required(true);

    msg_in.set_string_default_required("abc123");
    msg_in.set_bytes_default_required(goby::util::hex_decode("00112233aabbcc1234"));
    
    msg_in.set_enum_default_required(ENUM_C);
    msg_in.mutable_msg_default_required()->set_val(++i + 0.3);
    msg_in.mutable_msg_default_required()->mutable_msg()->set_val(++i);

    
    for(int j = 0; j < 2; ++j)
    {
        msg_in.add_double_default_repeat(++i + 0.1);
        msg_in.add_float_default_repeat(++i + 0.2);
        
        msg_in.add_int32_default_repeat(++i);
        msg_in.add_int64_default_repeat(-++i);
        msg_in.add_uint32_default_repeat(++i);
        msg_in.add_uint64_default_repeat(++i);
        msg_in.add_sint32_default_repeat(-++i);
        msg_in.add_sint64_default_repeat(++i);
        msg_in.add_fixed32_default_repeat(++i);
        msg_in.add_fixed64_default_repeat(++i);
        msg_in.add_sfixed32_default_repeat(++i);
        msg_in.add_sfixed64_default_repeat(-++i);
        
        msg_in.add_bool_default_repeat(true);
        
        msg_in.add_string_default_repeat("abc123");

        if(j)
            msg_in.add_bytes_default_repeat(goby::util::hex_decode("00aabbcc"));
        else
            msg_in.add_bytes_default_repeat(goby::util::hex_decode("ffeedd12"));
        
        msg_in.add_enum_default_repeat(static_cast<Enum1>((++i % 3) + 1));
        EmbeddedMsg1* em_msg = msg_in.add_msg_default_repeat();
        em_msg->set_val(++i + 0.3);
        em_msg->mutable_msg()->set_val(++i);
    }
    
    codec->info(msg_in.GetDescriptor(), &std::cout);    

    std::ofstream fout("/tmp/testmessage.pb");
    msg_in.SerializeToOstream(&fout);
    
    
    std::cout << "Message in:\n" << msg_in.DebugString() << std::endl;
     
    codec->validate(msg_in.GetDescriptor());

    std::cout << "Try encode..." << std::endl;
    std::string bytes;
    codec->encode(&bytes, msg_in);
    std::cout << "... got bytes (hex): " << goby::util::hex_encode(bytes) << std::endl;

    std::cout << "Try decode..." << std::endl;
    decode_check(bytes);

    // make sure Goby defaults stay wire compatible
    decode_check(goby::util::hex_decode("047f277b9628060000b95660c0b0188000d8c0132858800008000dc2c4c6626466024488cca8ee324bd05c3f23af0000ad9112a09509788013e0820b18e0005ed0204c6c2c4666062042644675975982c65235f10a00ad718a5801000000905f27121600000000a0170050640300309201001a0b00007d320a0000a61a0070b20100a81b00d09c6f0000a0401026361643102636160300f0dfbd5b2280ea2e330f3da59a2100aabfa55a000000000000000000000000"));

    // run a bunch of tests with random strings
    std::string random = bytes;
    for(unsigned i = 0; i < 10; ++i)
    {    
        random[(rand() % (bytes.size()-1)+1)] = rand() % 256;
        std::cout << "Using junk bytes: " << goby::util::hex_encode(random) << std::endl;
        
        try
        {
            TestMsg msg_out;
            codec->decode(random, &msg_out);
        }
        catch(goby::acomms::DCCLException&)
        {
        }
    }
    
    std::cout << "all tests passed" << std::endl;
}


void decode_check(const std::string& encoded)
{
    TestMsg msg_out;
    codec->decode(encoded, &msg_out);
    
    std::cout << "... got Message out:\n" << msg_out.DebugString() << std::endl;

    // truncate to "max_length" as codec should do
    msg_in.set_string_default_repeat(0,"abc1");
    msg_in.set_string_default_repeat(1,"abc1");
    
    assert(msg_in.SerializeAsString() == msg_out.SerializeAsString());
}
