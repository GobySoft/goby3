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

#include "identifier.h"

const char goby::middleware::InterProcessIdentifierManager::delimiter{'/'};
const char goby::middleware::InterProcessIdentifierManager::delimiter_substitute{
    0x1a}; // old ASCII substitute
const char goby::middleware::InterProcessIdentifierManager::end_delimiter{'\0'};

const std::string goby::middleware::InterProcessIdentifierManager::delimiter_str_{delimiter};

std::string goby::middleware::InterProcessIdentifierManager::make_identifier(
    const std::string& type_name, int scheme, const std::string& group, IdentifierWildcard wildcard,
    const std::string& process,
    std::unordered_map<int, std::string>* schemes_buffer /* = nullptr */,
    std::unordered_map<std::thread::id, std::string>* threads_buffer /*= nullptr*/)
{
    // swap out delimiter with substitute
    std::string sanitized_type_name = type_name;
    std::replace(sanitized_type_name.begin(), sanitized_type_name.end(), delimiter,
                 delimiter_substitute);
    std::string sanitized_group_name = group;
    std::replace(sanitized_group_name.begin(), sanitized_group_name.end(), delimiter,
                 delimiter_substitute);
    switch (wildcard)
    {
        default:
        case IdentifierWildcard::NO_WILDCARDS:
        {
            auto thread = std::this_thread::get_id();
            return (
                delimiter_str_ + sanitized_group_name + delimiter_str_ +
                (schemes_buffer ? id_component(scheme, *schemes_buffer)
                                : std::string(identifier_part_to_string(scheme) + delimiter_str_)) +
                sanitized_type_name + delimiter_str_ + process + delimiter_str_ +
                (threads_buffer ? id_component(thread, *threads_buffer)
                                : std::string(identifier_part_to_string(thread) + delimiter_str_)));
        }
        case IdentifierWildcard::THREAD_WILDCARD:
        {
            return (delimiter_str_ + sanitized_group_name + delimiter_str_ +
                    (schemes_buffer
                         ? id_component(scheme, *schemes_buffer)
                         : std::string(identifier_part_to_string(scheme) + delimiter_str_)) +
                    sanitized_type_name + delimiter_str_ + process + delimiter_str_);
        }
        case IdentifierWildcard::PROCESS_THREAD_WILDCARD:
        {
            return (delimiter_str_ + sanitized_group_name + delimiter_str_ +
                    (schemes_buffer
                         ? id_component(scheme, *schemes_buffer)
                         : std::string(identifier_part_to_string(scheme) + delimiter_str_)) +
                    sanitized_type_name + delimiter_str_);
        }
    }
}
std::tuple<std::string, int, std::string, int, std::size_t>
goby::middleware::InterProcessIdentifierManager::parse_identifier(const std::string& identifier)
{
    enum
    {
        POS_GROUP = 0,
        POS_SCHEME = 1,
        POS_TYPE = 2,
        POS_PROCESS = 3,
        POS_THREAD = 4,
        POS_MAX = POS_THREAD
    };

    const int number_elements = POS_MAX + 1;
    std::string::size_type previous_delimiter = 0;
    std::vector<std::string> elem;
    for (auto i = 0; i < number_elements; ++i)
    {
        auto delimiter_pos = identifier.find(delimiter, previous_delimiter + 1);
        elem.push_back(
            identifier.substr(previous_delimiter + 1, delimiter_pos - (previous_delimiter + 1)));
        previous_delimiter = delimiter_pos;
    }

    auto& group = elem[POS_GROUP];
    auto& type = elem[POS_TYPE];
    std::replace(type.begin(), type.end(), delimiter_substitute, delimiter);
    std::replace(group.begin(), group.end(), delimiter_substitute, delimiter);
    return std::make_tuple(
        elem[POS_GROUP], middleware::MarshallingScheme::from_string(elem[POS_SCHEME]),
        elem[POS_TYPE], std::stoi(elem[POS_PROCESS]), std::stoull(elem[POS_THREAD], nullptr, 16));
}
