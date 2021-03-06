// Copyright 2017-2021:
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

#ifndef GOBY_MIDDLEWARE_LOG_LOG_ENTRY_H
#define GOBY_MIDDLEWARE_LOG_LOG_ENTRY_H

#include <boost/bimap.hpp> // for bimap
#include <boost/crc.hpp>   // for crc_32_type
#include <cstdint>         // for uint16_t, uint32_t, uint64_t, uin...
#include <functional>      // for function
#include <istream>         // for ostream, istream, basic_ostream::...
#include <limits>          // for numeric_limits
#include <map>             // for map
#include <stdexcept>       // for runtime_error
#include <string>          // for string, allocator, operator+, ope...
#include <utility>         // for move
#include <vector>          // for vector

#include "goby/middleware/group.h" // for Group, DynamicGroup
#include "goby/time/system_clock.h"

namespace goby
{
namespace middleware
{
namespace log
{
class LogException : public std::runtime_error
{
  public:
    LogException(const std::string& s) : std::runtime_error(s){};
};

template <int Bytes> struct uint
{
};
template <> struct uint<1>
{
    using type = std::uint8_t;
};
template <> struct uint<2>
{
    using type = std::uint16_t;
};
template <> struct uint<4>
{
    using type = std::uint32_t;
};
template <> struct uint<8>
{
    using type = std::uint64_t;
};

struct LogFilter
{
    int scheme;
    std::string group;
    std::string type;
};

inline bool operator<(const LogFilter& a, const LogFilter& b)
{
    if (a.scheme == b.scheme)
    {
        if (a.group == b.group)
            return a.type < b.type;
        else
            return a.group < b.group;
    }
    else
    {
        return a.scheme < b.scheme;
    }
}

//inline bool operator==(const LogFilter& a, const LogFilter& b)
//{ return a.scheme == b.scheme && a.group == b.group && a.type == b.type; }

class LogEntry
{
  public:
    static constexpr int magic_bytes_{4};
    static constexpr int size_bytes_{4};
    static constexpr int scheme_bytes_{2};
    static constexpr int group_bytes_{2};
    static constexpr int type_bytes_{2};
    static constexpr int timestamp_bytes_{8};
    static constexpr int crc_bytes_{4};
    static constexpr uint<scheme_bytes_>::type scheme_group_index_{0xFFFF};
    static constexpr uint<scheme_bytes_>::type scheme_type_index_{0xFFFE};

    static constexpr int version_bytes_{4};
    static constexpr int compiled_current_version{3};
    static int current_version_;
    // "invalid_version" until version is read or written
    static uint<version_bytes_>::type version_;
    static constexpr decltype(version_) invalid_version{0};

    static std::map<int, std::function<void(const std::string& type)>> new_type_hook;
    static std::map<int, std::function<void(const Group& group)>> new_group_hook;

    static std::map<LogFilter, std::function<void(const std::vector<unsigned char>& data)>>
        filter_hook;

  public:
    LogEntry(std::vector<unsigned char> data, int scheme, std::string type, const Group& group,
             goby::time::SystemClock::time_point timestamp = goby::time::SystemClock::now())
        : data_(std::move(data)),
          scheme_(scheme),
          type_(std::move(type)),
          group_(std::string(group)),
          timestamp_(std::move(timestamp))
    {
    }

    LogEntry() : group_("") {}
    void parse_version(std::istream* s);
    void parse(std::istream* s);

    // used by the unit tests to override version numbers
    static void set_current_version(decltype(version_) version) { current_version_ = version; }

    // [GBY3][size: 4][scheme: 2][group: 2][type: 2][timestamp: 8][data][crc32: 4]
    // if scheme == 0xFFFF what follows is not data, but the string value for the group index
    // if scheme == 0xFFFE what follows is not data, but the string value for the group index
    void serialize(std::ostream* s) const;

    const std::vector<unsigned char>& data() const { return data_; }
    int scheme() const { return scheme_; }
    const std::string& type() const { return type_; }
    const Group& group() const { return group_; }
    const goby::time::SystemClock::time_point& timestamp() const { return timestamp_; }

    static void reset()
    {
        groups_.clear();
        types_.clear();
        new_type_hook.clear();
        new_group_hook.clear();
        filter_hook.clear();

        group_index_ = 1;
        type_index_ = 1;
        version_ = invalid_version;
        current_version_ = compiled_current_version;
    }

  private:
    void _serialize(std::ostream* s, uint<scheme_bytes_>::type scheme,
                    uint<group_bytes_>::type group_index, uint<type_bytes_>::type type_index,
                    const char* data, int data_size) const;

    template <typename Unsigned>
    Unsigned read_one(std::istream* s, boost::crc_32_type* crc = nullptr)
    {
        auto size = std::numeric_limits<Unsigned>::digits / 8;
        std::string str(size, '\0');
        s->read(&str[0], size);
        if (crc)
            crc->process_bytes(&str[0], size);

        return string_to_netint<Unsigned>(str);
    }

    template <typename Unsigned> std::string netint_to_string(Unsigned u) const
    {
        auto size = std::numeric_limits<Unsigned>::digits / 8;
        std::string s(size, '\0');
        for (int i = 0; i < size; ++i) s[i] = (u >> (size - (i + 1)) * 8) & 0xff;
        return s;
    }

    template <typename Unsigned> Unsigned string_to_netint(std::string s) const
    {
        Unsigned u(0);
        std::string::size_type size = std::numeric_limits<Unsigned>::digits / 8;
        if (s.size() > size)
            s.erase(0, s.size() - size);
        if (s.size() < size)
            s.insert(0, size - s.size(), '\0');

        for (decltype(size) i = 0; i < size; ++i)
            u |= static_cast<Unsigned>(s[i] & 0xff) << ((size - (i + 1)) * 8);
        return u;
    }

  private:
    std::vector<unsigned char> data_;
    uint<scheme_bytes_>::type scheme_;
    std::string type_;
    DynamicGroup group_;
    goby::time::SystemClock::time_point timestamp_;

    // map (scheme -> map (group_name -> group_index)
    static std::map<int, boost::bimap<std::string, uint<group_bytes_>::type>> groups_;
    static uint<group_bytes_>::type group_index_;

    // map (scheme -> map (group_name -> group_index)
    static std::map<int, boost::bimap<std::string, uint<type_bytes_>::type>> types_;
    static uint<type_bytes_>::type type_index_;

    const std::string magic_{"GBY3"};
};

} // namespace log
} // namespace middleware
} // namespace goby

#endif
