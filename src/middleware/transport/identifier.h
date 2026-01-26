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
    NO_WILDCARDS,
    THREAD_WILDCARD,
    PROCESS_THREAD_WILDCARD
};

template <char delimiter, char delimiter_substitute> class IdentifierManager
{
  public:
    static std::string
    make_identifier(const std::string& type_name, int scheme, const std::string& group,
                    IdentifierWildcard wildcard, const std::string& process,
                    std::unordered_map<int, std::string>* schemes_buffer = nullptr,
                    std::unordered_map<std::thread::id, std::string>* threads_buffer = nullptr)
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
                return (delimiter_str_ + sanitized_group_name + delimiter_str_ +
                        (schemes_buffer
                             ? id_component(scheme, *schemes_buffer)
                             : std::string(identifier_part_to_string(scheme) + delimiter_str_)) +
                        sanitized_type_name + delimiter_str_ + process + delimiter_str_ +
                        (threads_buffer
                             ? id_component(thread, *threads_buffer)
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

  protected:
    template <typename Data, int scheme>
    std::string _make_identifier(const goby::middleware::Group& group, IdentifierWildcard wildcard)
    {
        return _make_identifier(middleware::SerializerParserHelper<Data, scheme>::type_name(),
                                scheme, group, wildcard);
    }

    std::string _make_fully_qualified_identifier(const std::string& type_name, int scheme,
                                                 const std::string& group)
    {
        return _make_identifier(type_name, scheme, group, IdentifierWildcard::THREAD_WILDCARD) +
               id_component(std::this_thread::get_id(), threads_);
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

    // group, scheme, type, process, thread
    std::tuple<std::string, int, std::string, int, std::size_t> static parse_identifier(
        const std::string& identifier)
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
            elem.push_back(identifier.substr(previous_delimiter + 1,
                                             delimiter_pos - (previous_delimiter + 1)));
            previous_delimiter = delimiter_pos;
        }

        auto& group = elem[POS_GROUP];
        auto& type = elem[POS_TYPE];
        std::replace(type.begin(), type.end(), delimiter_substitute, delimiter);
        std::replace(group.begin(), group.end(), delimiter_substitute, delimiter);
        return std::make_tuple(elem[POS_GROUP],
                               middleware::MarshallingScheme::from_string(elem[POS_SCHEME]),
                               elem[POS_TYPE], std::stoi(elem[POS_PROCESS]),
                               std::stoull(elem[POS_THREAD], nullptr, 16));
    }

    // scheme
    static std::string identifier_part_to_string(int i)
    {
        return middleware::MarshallingScheme::to_string(i);
    }
    static std::string identifier_part_to_string(std::thread::id i)
    {
        return goby::middleware::thread_id(i);
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

template <char delimiter, char delimiter_substitute>
const std::string IdentifierManager<delimiter, delimiter_substitute>::delimiter_str_{delimiter};

} // namespace middleware
} // namespace goby
