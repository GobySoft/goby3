
#ifndef GOBY_MIDDLEWARE_TRANSPORT_DETAIL_STATIC_GROUP_NAMES_H
#define GOBY_MIDDLEWARE_TRANSPORT_DETAIL_STATIC_GROUP_NAMES_H

#include <array>

namespace goby::middleware::detail
{

struct DefaultInterprocessTag
{
    inline static constexpr const char prefix[] = "goby::middleware::interprocess";
};

struct ZeromqInterprocessTag
{
    inline static constexpr const char prefix[] = "goby::middleware::interprocess::zeromq";
};

struct ZeromqIntermoduleTag
{
    inline static constexpr const char prefix[] = "goby::middleware::intermodule::zeromq";
};

struct UdpmInterprocessTag
{
    inline static constexpr const char prefix[] = "goby::middleware::interprocess::udpm";
};

struct UdpmIntermoduleTag
{
    inline static constexpr const char prefix[] = "goby::middleware::intermodule::udpm";
};

// helper for concatenating prefix to group name
template <std::size_t N1, std::size_t N2>
constexpr auto concat(const char (&a)[N1], const char (&b)[N2])
{
    std::array<char, N1 + N2 - 1> out{};
    for (std::size_t i = 0; i < N1 - 1; ++i) out[i] = a[i];
    for (std::size_t i = 0; i < N2; ++i) out[i + N1 - 1] = b[i];
    return out;
}

} // namespace goby::middleware::detail

#endif
