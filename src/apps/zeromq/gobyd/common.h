// Copyright 2011-2023:
//   GobySoft, LLC (2013-)
//   Massachusetts Institute of Technology (2007-2014)
//   Community contributors (see AUTHORS file)
// File authors:
//   Toby Schneider <toby@gobysoft.org>
//
//
// This file is part of the Goby Underwater Autonomy Project Binaries
// ("The Goby Binaries").
//
// The Goby Binaries are free software: you can redistribute them and/or modify
// them under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 2 of the License, or
// (at your option) any later version.
//
// The Goby Binaries are distributed in the hope that they will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with Goby.  If not, see <http://www.gnu.org/licenses/>.

#ifndef GOBYD_COMMON
#define GOBYD_COMMON

// shared between gobyd and standalone goby_intervehicle_portal

namespace goby
{
namespace apps
{
namespace zeromq
{
template <typename Config> void process_intervehicle_config(Config& cfg)
{
    if (cfg.has_intervehicle())
    {
        auto& intervehicle = *cfg.mutable_intervehicle();
        if (intervehicle.has_persist_subscriptions())
        {
            auto& p = *intervehicle.mutable_persist_subscriptions();
            if (!p.has_name())
                p.set_name(cfg.interprocess().platform());
        }
    }
}

} // namespace zeromq
} // namespace apps
} // namespace goby

#endif
