// Copyright 2009-2018 Toby Schneider (http://gobysoft.org/index.wt/people/toby)
//                     GobySoft, LLC (2013-)
//                     Massachusetts Institute of Technology (2007-2014)
//                     Community contributors (see AUTHORS file)
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

#ifndef ZeroMQPacket20160413H
#define ZeroMQPacket20160413H

#include <sstream>

namespace goby
{
namespace common
{
std::string zeromq_packet_make_header(int marshalling_scheme, const std::string& identifier)
{
    std::string zmq_filter;

    google::protobuf::uint32 marshalling_int =
        static_cast<google::protobuf::uint32>(marshalling_scheme);

    for (int i = 0, n = BITS_IN_UINT32 / BITS_IN_BYTE; i < n; ++i)
    { zmq_filter.push_back((marshalling_int >> (BITS_IN_BYTE * (n - i - 1))) & 0xFF); }
    zmq_filter += identifier + '\0';

    return zmq_filter;
}

/// \brief Encodes a packet for Goby over ZeroMQ
void zeromq_packet_encode(std::string* raw, int marshalling_scheme, const std::string& identifier,
                          const std::string& body)
{
    *raw = zeromq_packet_make_header(marshalling_scheme, identifier);
    *raw += body;
}

/// \brief Decodes a packet for Goby over ZeroMQ
void zeromq_packet_decode(const std::string& raw, int* marshalling_scheme, std::string* identifier,
                          std::string* body)
{
    // byte size of marshalling id
    const unsigned MARSHALLING_SIZE = BITS_IN_UINT32 / BITS_IN_BYTE;

    if (raw.size() < MARSHALLING_SIZE)
        throw(std::runtime_error("Message is too small"));

    google::protobuf::uint32 marshalling_int = 0;
    for (int i = 0, n = MARSHALLING_SIZE; i < n; ++i)
    {
        marshalling_int <<= BITS_IN_BYTE;
        marshalling_int ^= raw[i];
    }

    *marshalling_scheme = marshalling_int;

    *identifier = raw.substr(MARSHALLING_SIZE, raw.find('\0', MARSHALLING_SIZE) - MARSHALLING_SIZE);

    // +1 for null terminator
    const int HEADER_SIZE = MARSHALLING_SIZE + identifier->size() + 1;
    *body = raw.substr(HEADER_SIZE);
}
} // namespace common
} // namespace goby

#endif
