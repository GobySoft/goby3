
#pragma once

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
