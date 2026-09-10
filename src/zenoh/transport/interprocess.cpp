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

#include "goby/exception.h"

#include "goby/zenoh/transport/interprocess.h"

namespace
{
std::string json5_string_array(const google::protobuf::RepeatedPtrField<std::string>& values)
{
    std::string array = "[";
    for (int i = 0, n = values.size(); i < n; ++i)
    {
        if (i)
            array += ",";
        array += "\"" + values.Get(i) + "\"";
    }
    array += "]";
    return array;
}
} // namespace

std::string goby::zenoh::make_key_root(const protobuf::InterProcessPortalConfig& cfg,
                                       const std::string& layer)
{
    return detail::escape_chunk(cfg.key_prefix()) + "/" + detail::escape_chunk(cfg.platform()) +
           "/" + layer;
}

zenoh::Session goby::zenoh::open_session(const protobuf::InterProcessPortalConfig& cfg)
{
    auto config = ::zenoh::Config::create_default();

    switch (cfg.mode())
    {
        case protobuf::InterProcessPortalConfig::PEER:
            config.insert_json5("mode", "\"peer\"");
            break;
        case protobuf::InterProcessPortalConfig::CLIENT:
            config.insert_json5("mode", "\"client\"");
            break;
        case protobuf::InterProcessPortalConfig::ROUTER:
            config.insert_json5("mode", "\"router\"");
            break;
    }

    if (cfg.connect_endpoint_size() > 0)
        config.insert_json5("connect/endpoints", json5_string_array(cfg.connect_endpoint()));
    if (cfg.listen_endpoint_size() > 0)
        config.insert_json5("listen/endpoints", json5_string_array(cfg.listen_endpoint()));

    config.insert_json5("scouting/multicast/enabled", cfg.multicast_scouting() ? "true" : "false");
    if (cfg.has_scouting_interface())
        config.insert_json5("scouting/multicast/interface", "\"" + cfg.scouting_interface() + "\"");

    // applied last so that it can correct anything set above
    for (const auto& override_cfg : cfg.json5_override())
        config.insert_json5(override_cfg.key(), override_cfg.value());

    try
    {
        return ::zenoh::Session::open(std::move(config));
    }
    catch (const std::exception& e)
    {
        // Zenoh reports only a numeric code; the endpoints are what the user can act on, and its
        // default listen endpoint (tcp/[::]:0) fails outright where IPv6 is unavailable
        std::string endpoints;
        for (const auto& endpoint : cfg.listen_endpoint()) endpoints += " " + endpoint;
        throw(goby::Exception(
            std::string("Failed to open Zenoh session: ") + e.what() +
            ". Listen endpoints:" + (endpoints.empty() ? " (Zenoh default)" : endpoints) +
            ". If IPv6 is unavailable, set interprocess { listen_endpoint: \"tcp/0.0.0.0:0\" }"));
    }
}
