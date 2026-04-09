
namespace goby::middleware::detail
{

struct DefaultInterprocessTag
{
    inline static constexpr const char prefix[] = "goby::middleware::interprocess";
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
