// Copyright 2009-2017 Toby Schneider (http://gobysoft.org/index.wt/people/toby)
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

// encodes/decodes a string using the DCCL codec library
// assumes prior knowledge of the message format (required fields)

#include "goby/acomms/dccl.h" // for DCCLCodec
#include "simple.pb.h" // for `Simple` protobuf message defined in simple.proto
#include "goby/util/binary.h" // for goby::util::hex_encode
#include "goby/acomms/acomms_helpers.h" // for operator<< of google::protobuf::Message

using goby::acomms::operator<<;

int main()
{
    goby::acomms::DCCLCodec* dccl = goby::acomms::DCCLCodec::get();

    // validate the Simple protobuf message type as a valid DCCL message type
    dccl->validate<Simple>();
    
    // read message content (in this case from the command line)
    Simple message;
    std::cout << "input a string to send up to 10 characters: " << std::endl;
    getline(std::cin, *message.mutable_telegram());

    std::cout << "passing message to encoder:\n" << message << std::endl;

    // encode the message to a byte string
    std::string bytes;
    dccl->encode(&bytes, message);
    
    std::cout << "received hexadecimal string: " << goby::util::hex_encode(bytes) << std::endl;

    message.Clear();
    // input contents right back to decoder
    std::cout << "passed hexadecimal string to decoder: " << goby::util::hex_encode(bytes) << std::endl;

    dccl->decode(bytes, &message);
    
    std::cout << "received message:" << message << std::endl;

    
    return 0;
}

