#pragma once

namespace goby
{
namespace middleware
{
namespace detail
{
template <typename Config> inline Config make_interprocess_config(Config cfg, std::string app_name)
{
    cfg.set_client_name(app_name);
    return cfg;
}

} // namespace detail
} // namespace middleware
} // namespace goby
