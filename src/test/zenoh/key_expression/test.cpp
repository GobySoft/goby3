// Copyright 2026:
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
// along with this software.  If not, see <http://www.gnu.org/licenses/>.

// Tests the mapping between Goby interprocess identifiers and Zenoh key expressions. A publisher
// writes a key and a subscriber reads it back, so any character that survives escaping but not
// unescaping (or vice versa) corrupts the group or type name a message is delivered under.

#include <cassert>
#include <iostream>
#include <string>

#include "goby/middleware/transport/identifier.h"
#include "goby/zenoh/transport/detail/key_expression.h"

using goby::zenoh::detail::escape_chunk;
using goby::zenoh::detail::identifier_to_key;
using goby::zenoh::detail::key_to_identifier;
using goby::zenoh::detail::unescape_chunk;

const std::string root{"goby/testplatform/interprocess"};

// A chunk must survive the trip out to a key expression and back unchanged.
void test_chunk_round_trip(const std::string& chunk)
{
    const std::string escaped = escape_chunk(chunk);

    // Zenoh rejects these outright, and '/' would silently split the chunk in two
    assert(escaped.find('*') == std::string::npos);
    assert(escaped.find('?') == std::string::npos);
    assert(escaped.find('#') == std::string::npos);
    assert(escaped.find('$') == std::string::npos);
    assert(escaped.find('/') == std::string::npos);

    assert(unescape_chunk(escaped) == chunk);
}

// A published identifier must come back byte for byte, including the trailing delimiter that the
// interprocess layer parses against.
void test_identifier_round_trip(const std::string& identifier)
{
    const std::string key = identifier_to_key(root, identifier, false);
    assert(key.compare(0, root.size(), root) == 0);
    assert(key_to_identifier(root, key) == identifier);
}

int main()
{
    for (const auto& chunk :
         {std::string("NavigationReport"), std::string("goby.middleware.protobuf.IOData"),
          std::string("group_with_underscore"), std::string("group-with-dash"), std::string("*"),
          std::string("?"), std::string("#"), std::string("$"), std::string("/"), std::string("%"),
          std::string("%2F"), std::string("a*b?c#d$e/f%g"), std::string("@admin"),
          std::string("with space"), std::string("\x1a"), std::string("")})
    {
        test_chunk_round_trip(chunk);
    }

    // a chunk of only reserved characters is still non-empty once escaped, as Zenoh requires
    assert(!escape_chunk("/").empty());
    assert(escape_chunk("/") == "%2F");
    assert(escape_chunk("%") == "%25");

    // escaping must be idempotent under unescape, not under escape: an already-escaped chunk is
    // escaped again rather than being mistaken for its decoded form
    assert(escape_chunk("%2F") == "%252F");
    assert(unescape_chunk(escape_chunk("%2F")) == "%2F");

    // a malformed escape is left alone rather than dropping characters
    assert(unescape_chunk("%") == "%");
    assert(unescape_chunk("%2") == "%2");
    assert(unescape_chunk("%ZZ") == "%ZZ");

    const char d = goby::middleware::InterProcessIdentifierManager::delimiter;
    const std::string dt(1, d);

    test_identifier_round_trip(dt + "NavigationGroup" + dt + "PROTOBUF" + dt + "NavReport" + dt +
                               "8134" + dt + "a3f0" + dt);
    test_identifier_round_trip(dt + "group/with/slashes" + dt + "PROTOBUF" + dt +
                               "goby.middleware.protobuf.IOData" + dt + "1" + dt + "0" + dt);
    test_identifier_round_trip(dt + "group*with?wildcards" + dt + "DCCL" + dt + "Type" + dt + "2" +
                               dt + "ff" + dt);

    // the trailing end delimiter a publication carries is not part of the key
    {
        const std::string identifier =
            dt + "g" + dt + "PROTOBUF" + dt + "T" + dt + "5" + dt + "b" + dt;
        const std::string published =
            identifier + goby::middleware::InterProcessIdentifierManager::end_delimiter;
        assert(identifier_to_key(root, published, false) ==
               identifier_to_key(root, identifier, false));
    }

    // a subscription is wildcarded over the process and thread chunks a publication adds
    {
        const std::string subscribe_id = dt + "g" + dt + "PROTOBUF" + dt + "T" + dt;
        assert(identifier_to_key(root, subscribe_id, true) == root + "/g/PROTOBUF/T/**");
    }

    // a key outside the root belongs to another platform or layer and must be rejected
    assert(key_to_identifier(root, "goby/otherplatform/interprocess/g/PROTOBUF/T/1/a").empty());
    assert(key_to_identifier(root, "goby/testplatform/intermodule/g/PROTOBUF/T/1/a").empty());
    assert(key_to_identifier(root, root).empty());

    std::cout << "all tests passed" << std::endl;
    return 0;
}
