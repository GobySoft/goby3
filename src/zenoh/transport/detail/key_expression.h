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

#ifndef GOBY_ZENOH_TRANSPORT_DETAIL_KEY_EXPRESSION_H
#define GOBY_ZENOH_TRANSPORT_DETAIL_KEY_EXPRESSION_H

#include <string>
#include <vector>

#include "goby/middleware/transport/identifier.h"

namespace goby
{
namespace zenoh
{
namespace detail
{

/// \brief Characters that cannot appear in a key expression chunk.
///
/// Zenoh rejects '*', '?', '#' and '$' outright; '/' would silently introduce a new chunk. '%'
/// joins them because it introduces an escape sequence here.
inline bool is_reserved(char c)
{
    return c == '*' || c == '?' || c == '#' || c == '$' || c == '/' || c == '%';
}

/// \brief Percent-encode the characters a key expression chunk cannot carry.
inline std::string escape_chunk(const std::string& chunk)
{
    static constexpr const char hex[] = "0123456789ABCDEF";
    std::string escaped;
    escaped.reserve(chunk.size());
    for (char c : chunk)
    {
        if (is_reserved(c))
        {
            escaped += '%';
            escaped += hex[(static_cast<unsigned char>(c) >> 4) & 0xF];
            escaped += hex[static_cast<unsigned char>(c) & 0xF];
        }
        else
        {
            escaped += c;
        }
    }
    return escaped;
}

/// \brief Invert escape_chunk(). A trailing or malformed escape is passed through unchanged.
inline std::string unescape_chunk(const std::string& chunk)
{
    auto hex_value = [](char c) -> int
    {
        if (c >= '0' && c <= '9')
            return c - '0';
        if (c >= 'A' && c <= 'F')
            return c - 'A' + 10;
        if (c >= 'a' && c <= 'f')
            return c - 'a' + 10;
        return -1;
    };

    std::string unescaped;
    unescaped.reserve(chunk.size());
    for (std::size_t i = 0; i < chunk.size(); ++i)
    {
        int hi = -1, lo = -1;
        if (chunk[i] == '%' && i + 2 < chunk.size() && (hi = hex_value(chunk[i + 1])) >= 0 &&
            (lo = hex_value(chunk[i + 2])) >= 0)
        {
            unescaped += static_cast<char>((hi << 4) | lo);
            i += 2;
        }
        else
        {
            unescaped += chunk[i];
        }
    }
    return unescaped;
}

/// \brief Split a Goby interprocess identifier into its '/'-delimited components.
///
/// Handles both the fully qualified form (group, scheme, type, process, thread) and the wildcard
/// forms used to subscribe, which stop early. Anything from the end delimiter onward is ignored.
inline std::vector<std::string> split_identifier(const std::string& identifier)
{
    const char delimiter = middleware::InterProcessIdentifierManager::delimiter;
    const auto end = identifier.find(middleware::InterProcessIdentifierManager::end_delimiter);

    std::vector<std::string> components;
    std::size_t start = 0;
    while (start < end && start < identifier.size())
    {
        if (identifier[start] == delimiter)
        {
            ++start;
            continue;
        }
        auto next = identifier.find(delimiter, start);
        if (next > end)
            next = end;
        components.push_back(identifier.substr(start, next - start));
        start = (next == std::string::npos) ? identifier.size() : next + 1;
    }
    return components;
}

/// \brief Rebuild a Goby identifier from key expression chunks, with the trailing delimiter Goby expects.
inline std::string join_identifier(const std::vector<std::string>& components)
{
    const char delimiter = middleware::InterProcessIdentifierManager::delimiter;
    std::string identifier;
    for (const auto& component : components)
    {
        identifier += delimiter;
        identifier += component;
    }
    identifier += delimiter;
    return identifier;
}

/// \brief Map a Goby identifier onto a key expression below \c root.
///
/// \param root leading chunks shared by every key on this layer, without a trailing '/'
/// \param identifier Goby identifier, fully qualified or wildcarded
/// \param wildcard_suffix append "/**" so that a subscription made with a wildcarded identifier
///        matches the process and thread chunks a publication carries
inline std::string identifier_to_key(const std::string& root, const std::string& identifier,
                                     bool wildcard_suffix)
{
    std::string key = root;
    for (const auto& component : split_identifier(identifier))
    {
        key += '/';
        key += escape_chunk(component);
    }
    if (wildcard_suffix)
        key += "/**";
    return key;
}

/// \brief Invert identifier_to_key() for a received sample, whose key is always fully qualified.
///
/// \return the Goby identifier, or an empty string if the key does not lie below \c root
inline std::string key_to_identifier(const std::string& root, const std::string& key)
{
    if (key.size() <= root.size() || key.compare(0, root.size(), root) != 0 ||
        key[root.size()] != '/')
        return {};

    std::vector<std::string> components;
    std::size_t start = root.size() + 1;
    while (start <= key.size())
    {
        auto next = key.find('/', start);
        components.push_back(
            unescape_chunk(key.substr(start, next == std::string::npos ? next : next - start)));
        if (next == std::string::npos)
            break;
        start = next + 1;
    }
    return join_identifier(components);
}

} // namespace detail
} // namespace zenoh
} // namespace goby

#endif
