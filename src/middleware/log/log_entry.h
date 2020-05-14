// Copyright 2019-2020:
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

#ifndef LogEntry20171127H
#define LogEntry20171127H

#include <boost/bimap.hpp>
#include <boost/crc.hpp>
#include <cstdint>

#include "goby/exception.h"
#include "goby/util/debug_logger.h"

#include "goby/middleware/group.h"
#include "goby/middleware/marshalling/interface.h"

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
    typedef std::uint8_t type;
};
template <> struct uint<2>
{
    typedef std::uint16_t type;
};
template <> struct uint<4>
{
    typedef std::uint32_t type;
};
template <> struct uint<8>
{
    typedef std::uint64_t type;
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
    static constexpr int crc_bytes_{4};
    static constexpr uint<scheme_bytes_>::type scheme_group_index_{0xFFFF};
    static constexpr uint<scheme_bytes_>::type scheme_type_index_{0xFFFE};

    static constexpr int version_bytes_{4};
    static constexpr int current_version{2};
    // "invalid_version" until version is read or written
    static uint<version_bytes_>::type version_;
    static constexpr decltype(version_) invalid_version{0};

    static std::map<int, std::function<void(const std::string& type)> > new_type_hook;
    static std::map<int, std::function<void(const Group& group)> > new_group_hook;

    static std::map<LogFilter, std::function<void(const std::vector<unsigned char>& data)> >
        filter_hook;

  public:
    LogEntry(const std::vector<unsigned char>& data, int scheme, const std::string& type,
             const Group& group)
        : data_(data), scheme_(scheme), type_(type), group_(std::string(group))
    {
    }

    LogEntry() : group_("") {}
    void parse_version(std::istream* s);
    void parse(std::istream* s);

    // [GBY3][size: 4][scheme: 2][group: 2][type: 2][data][crc32: 4]
    // if scheme == 0xFFFF what follows is not data, but the string value for the group index
    // if scheme == 0xFFFE what follows is not data, but the string value for the group index
    void serialize(std::ostream* s) const;

    const std::vector<unsigned char>& data() const { return data_; }
    int scheme() const { return scheme_; }
    const std::string& type() const { return type_; }
    const Group& group() const { return group_; }
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
    }

  private:
    void _serialize(std::ostream* s, uint<scheme_bytes_>::type scheme,
                    uint<group_bytes_>::type group_index, uint<type_bytes_>::type type_index,
                    const char* data, int data_size) const
    {
        std::string group_str(netint_to_string(group_index));
        std::string type_str(netint_to_string(type_index));
        std::string scheme_str(netint_to_string(scheme));

        uint<size_bytes_>::type size =
            scheme_bytes_ + group_bytes_ + type_bytes_ + data_size + crc_bytes_;
        std::string size_str(netint_to_string(size));

        auto header = magic_ + size_str + scheme_str + group_str + type_str;
        s->write(header.data(), header.size());
        s->write(data, data_size);

        boost::crc_32_type crc;
        crc.process_bytes(header.data(), header.size());
        crc.process_bytes(data, data_size);

        uint<crc_bytes_>::type cs(crc.checksum());
        std::string cs_str(netint_to_string(cs));
        s->write(cs_str.data(), cs_str.size());
    }

    template <typename Unsigned> Unsigned read_one(std::istream* s, boost::crc_32_type* crc = 0)
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

        for (decltype(size) i = 0; i < size; ++i) u |= (s[i] & 0xff) << ((size - (i + 1)) * 8);
        return u;
    }

  private:
    std::vector<unsigned char> data_;
    uint<scheme_bytes_>::type scheme_;
    std::string type_;
    DynamicGroup group_;

    // map (scheme -> map (group_name -> group_index)
    static std::map<int, boost::bimap<std::string, uint<group_bytes_>::type> > groups_;
    static uint<group_bytes_>::type group_index_;

    // map (scheme -> map (group_name -> group_index)
    static std::map<int, boost::bimap<std::string, uint<type_bytes_>::type> > types_;
    static uint<type_bytes_>::type type_index_;

    const std::string magic_{"GBY3"};
};

} // namespace middleware
} // namespace goby
} // namespace log

#endif
