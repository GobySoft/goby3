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

#ifndef GOBY_MIDDLEWARE_TRANSPORT_IDENTIFIER_H
#define GOBY_MIDDLEWARE_TRANSPORT_IDENTIFIER_H

#include <algorithm>
#include <string>
#include <thread>
#include <unistd.h> // for getpid
#include <unordered_map>
#include <vector>

#include "goby/middleware/group.h"
#include "goby/middleware/marshalling/interface.h"            // for Seri...
#include "goby/middleware/transport/serialization_handlers.h" // for Seri...

namespace goby
{
namespace middleware
{

enum class IdentifierWildcard
{
    NO_WILDCARDS,           // fully qualified
    THREAD_WILDCARD,        // omit thread
    PROCESS_THREAD_WILDCARD // omit process and thread
};

struct InterProcessIdentity
{
    std::string group;
    int scheme;
    std::string type_name;
    int process_id;
    std::size_t thread_id;
};

// scheme
inline std::string identifier_part_to_string(int i)
{
    return middleware::MarshallingScheme::to_string(i);
}
inline std::string identifier_part_to_string(std::thread::id i)
{
    return goby::middleware::thread_id(i);
}

class InterProcessIdentifierManager
{
  public:
    const static char delimiter;
    const static char delimiter_substitute;
    const static char end_delimiter;
    
    static std::string
    make_identifier(const std::string& type_name, int scheme, const std::string& group,
                    IdentifierWildcard wildcard, const std::string& process,
                    std::unordered_map<int, std::string>* schemes_buffer = nullptr,
                    std::unordered_map<std::thread::id, std::string>* threads_buffer = nullptr);

    // group, scheme, type, process, thread
    std::tuple<std::string, int, std::string, int, std::size_t> static parse_identifier(
        const std::string& identifier);

  protected:
    template <typename Data, int scheme>
    std::string _make_identifier(const goby::middleware::Group& group, IdentifierWildcard wildcard)
    {
        return _make_identifier(middleware::SerializerParserHelper<Data, scheme>::type_name(),
                                scheme, group, wildcard);
    }

    template <typename Data, int scheme>
    std::string _make_identifier(const Data& d, const goby::middleware::Group& group,
                                 IdentifierWildcard wildcard)
    {
        return _make_identifier(middleware::SerializerParserHelper<Data, scheme>::type_name(d),
                                scheme, group, wildcard);
    }

    std::string _make_identifier(const std::string& type_name, int scheme, const std::string& group,
                                 IdentifierWildcard wildcard)
    {
        return make_identifier(type_name, scheme, group, wildcard, process_, &schemes_, &threads_);
    }

  private:
    /// Given key, find the string in the map, or create it (to_string) and store it, and return the string.
    template <typename Key>
    static const std::string& id_component(const Key& k, std::unordered_map<Key, std::string>& map)
    {
        auto it = map.find(k);
        if (it != map.end())
            return it->second;

        std::string v = identifier_part_to_string(k) + delimiter_str_;
        auto it_pair = map.insert(std::make_pair(k, v));
        return it_pair.first->second;
    }

  private:
    static const std::string delimiter_str_;
    const std::string process_{std::to_string(getpid())};
    std::unordered_map<int, std::string> schemes_;
    std::unordered_map<std::thread::id, std::string> threads_;
};

} // namespace middleware
} // namespace goby

#endif
