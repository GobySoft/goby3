// Copyright 2020:
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

#pragma once

#include <regex>

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
    explicit match_regex(std::string eol) : eol_regex_(eol) {}

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
