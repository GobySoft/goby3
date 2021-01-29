// Copyright 2020-2021:
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

#ifndef GOBY_MIDDLEWARE_IO_LINE_BASED_COMMON_H
#define GOBY_MIDDLEWARE_IO_LINE_BASED_COMMON_H

#include <atomic>   // for atomic
#include <locale>   // for ctype, use_facet, locale
#include <map>      // for map
#include <regex>    // for _NFA, match_results, regex, regex_search
#include <sstream>  // for basic_stringbuf<>::int_type, basic_stringbuf<>::...
#include <stddef.h> // for size_t
#include <string>   // for string
#include <utility>  // for make_pair, pair
#include <vector>   // for vector

namespace boost
{
namespace asio
{
template <typename T> struct is_match_condition;
}
} // namespace boost

namespace goby
{
namespace middleware
{
namespace io
{
/// \brief Provides a matching function object for the boost::asio::async_read_until based on a std::regex
class match_regex
{
  public:
    explicit match_regex(std::string eol) : eol_regex_(ctype_narrow_workaround(eol)) {}

    template <typename Iterator>
    std::pair<Iterator, bool> operator()(Iterator begin, Iterator end) const
    {
        std::match_results<Iterator> result;
        if (std::regex_search(begin, end, result, eol_regex_))
            return std::make_pair(begin + result.position() + result.length(), true);
        else
            return std::make_pair(begin, false);
    }

  private:
    std::string ctype_narrow_workaround(std::string eol)
    {
        // Thanks to  Boris Kolpackov:
        // A data race happens in the libstdc++ (as of GCC 7.2) implementation of the
        // ctype<ctype>::narrow() function (bug #77704).
        // We work around this by pre-initializing the global locale
        // facet internal cache.
        static std::atomic<bool> ctype_narrow_initialized{false};
        if (!ctype_narrow_initialized)
        {
            const std::ctype<char>& ct(std::use_facet<std::ctype<char>>(std::locale()));
            for (size_t i(0); i != 256; ++i) ct.narrow(static_cast<char>(i), '\0');
            ctype_narrow_initialized = true;
        }
        return eol;
    }

  private:
    std::regex eol_regex_;
};

} // namespace io
} // namespace middleware
} // namespace goby

namespace boost
{
namespace asio
{
template <> struct is_match_condition<goby::middleware::io::match_regex> : public boost::true_type
{
};
} // namespace asio
} // namespace boost

#endif
