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

// tests receiving one of several static types

#include "goby/acomms/dccl.h"
#include "goby/acomms/acomms_helpers.h"
#include "test.pb.h"


using goby::acomms::operator<<;

GobyMessage1 msg_in1;
GobyMessage2 msg_in2;
GobyMessage3 msg_in3;
goby::acomms::DCCLCodec* codec = goby::acomms::DCCLCodec::get();

void decode(const std::string& bytes);

int main(int argc, char* argv[])
{
    goby::glog.add_stream(goby::common::logger::DEBUG3, &std::cerr);
    goby::glog.set_name(argv[0]);


    codec->validate<GobyMessage1>();
    codec->validate<GobyMessage2>();
    codec->validate<GobyMessage3>();

    codec->info<GobyMessage1>(&goby::glog);
    codec->info<GobyMessage2>(&goby::glog);
    codec->info<GobyMessage3>(&goby::glog);

    
 
    msg_in1.set_int32_val(1);
    msg_in2.set_bool_val(false);
    msg_in3.set_string_val("string1");

    std::cout << "Try encode..." << std::endl;
    std::string bytes1, bytes2, bytes3;
    codec->encode(&bytes1, msg_in1);
    std::cout << "... got bytes for GobyMessage1 (hex): " << goby::util::hex_encode(bytes1) << std::endl;
    codec->encode(&bytes2, msg_in2);
    std::cout << "... got bytes for GobyMessage2 (hex): " << goby::util::hex_encode(bytes2) << std::endl;
    codec->encode(&bytes3, msg_in3);
    std::cout << "... got bytes for GobyMessage3 (hex): " << goby::util::hex_encode(bytes3) << std::endl;

    std::cout << "Try decode..." << std::endl;

    // mix up the order
    decode(bytes2);
    decode(bytes1);
    decode(bytes3);
    

    std::cout << "all tests passed" << std::endl;
}

void decode(const std::string& bytes)
{
    unsigned dccl_id = codec->id_from_encoded(bytes);
    
    if(dccl_id == codec->id<GobyMessage1>())
    {
        GobyMessage1 msg_out1;
        codec->decode(bytes, &msg_out1);

        std::cout << "Got..." << msg_out1 << std::endl;
        assert(msg_out1.SerializeAsString() == msg_in1.SerializeAsString());
    }
    else if(dccl_id == codec->id<GobyMessage2>())
    {
        GobyMessage2 msg_out2;
        codec->decode(bytes, &msg_out2);

        std::cout << "Got..." << msg_out2 << std::endl;
        assert(msg_out2.SerializeAsString() == msg_in2.SerializeAsString());
    }
    else if(dccl_id == codec->id<GobyMessage3>())
    {
        GobyMessage3 msg_out3;
        codec->decode(bytes, &msg_out3);
        
        std::cout << "Got..." << msg_out3 << std::endl;
        assert(msg_out3.SerializeAsString() == msg_in3.SerializeAsString());
    }
    
}
