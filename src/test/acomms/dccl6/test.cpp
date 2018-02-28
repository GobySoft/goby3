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

// tests fixed id header

#include "goby/acomms/dccl.h"
#include "test.pb.h"

using goby::acomms::operator<<;

int main(int argc, char* argv[])
{
    goby::glog.add_stream(goby::common::logger::DEBUG3, &std::cerr);
    goby::glog.set_name(argv[0]);
    
    goby::acomms::DCCLCodec* codec = goby::acomms::DCCLCodec::get();

    ShortIDMsg short_id_msg;
    codec->validate(short_id_msg.GetDescriptor());
    codec->info(short_id_msg.GetDescriptor(), &goby::glog);

    std::string encoded;
    assert(codec->size(short_id_msg) == 1);
    codec->encode(&encoded, short_id_msg);
    codec->decode(encoded, &short_id_msg);

    LongIDMsg long_id_msg;
    codec->validate(long_id_msg.GetDescriptor());
    codec->info(long_id_msg.GetDescriptor(), &goby::glog);
    assert(codec->size(long_id_msg) == 2);
    codec->encode(&encoded, long_id_msg);
    codec->decode(encoded, &long_id_msg);
    

    ShortIDEdgeMsg short_id_edge_msg;
    codec->validate(short_id_edge_msg.GetDescriptor());
    codec->info(short_id_edge_msg.GetDescriptor(), &goby::glog);
    assert(codec->size(short_id_edge_msg) == 1);
    codec->encode(&encoded, short_id_edge_msg);
    codec->decode(encoded, &short_id_edge_msg);

    LongIDEdgeMsg long_id_edge_msg;
    codec->validate(long_id_edge_msg.GetDescriptor());
    codec->info(long_id_edge_msg.GetDescriptor(), &goby::glog);
    codec->encode(&encoded, long_id_edge_msg);
    codec->decode(encoded, &long_id_edge_msg);
    assert(codec->size(long_id_edge_msg) == 2);

    TooLongIDMsg too_long_id_msg;
    // should fail validation
    try
    {
        codec->validate(too_long_id_msg.GetDescriptor());
        assert(false);
    }
    catch(goby::acomms::DCCLException& e)
    { }
    

    ShortIDMsgWithData short_id_msg_with_data;
    codec->validate(short_id_msg_with_data.GetDescriptor());
    codec->info(short_id_msg_with_data.GetDescriptor(), &goby::glog);

    short_id_msg_with_data.set_in_head(42);
    short_id_msg_with_data.set_in_body(37);
    codec->encode(&encoded, short_id_msg_with_data);
    codec->decode(encoded, &short_id_msg_with_data);

    
    std::cout << "all tests passed" << std::endl;
}

