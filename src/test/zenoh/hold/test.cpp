// Copyright 2026:
//   GobySoft, LLC (2013-)
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
// along with this software.  If not, see <http://www.gnu.org/licenses/>.

// Tests that the hold actually holds. A portal configured to wait for a client that does not exist
// yet must keep hold_state() true and must not put anything on the wire; once that client
// announces itself the hold must lift and everything published meanwhile must arrive, in order.
// A test that only checked the end state would pass just as well against a hold that never
// engaged, so the buffering is asserted while the hold is still on.

#include <cassert>
#include <chrono>
#include <iostream>
#include <vector>

#include "goby/middleware/marshalling/protobuf.h"
#include "goby/zenoh/transport/interprocess.h"

#include "goby/middleware/protobuf/io.pb.h"

using goby::middleware::protobuf::IOData;

constexpr goby::middleware::Group held{"HeldGroup"};

goby::zenoh::protobuf::InterProcessPortalConfig make_config(const std::string& client_name)
{
    goby::zenoh::protobuf::InterProcessPortalConfig cfg;
    cfg.set_platform("hold_test");
    cfg.set_client_name(client_name);
    // Zenoh's default listen endpoint is tcp/[::]:0, which fails where IPv6 is unavailable
    cfg.add_listen_endpoint("tcp/127.0.0.1:0");
    return cfg;
}

int main()
{
    std::vector<std::string> received;

    auto waiter_cfg = make_config("waiter");
    waiter_cfg.mutable_hold()->add_required_client("helper");
    goby::zenoh::InterProcessPortal<> waiter(waiter_cfg);

    waiter.subscribe<held, IOData>([&](const IOData& io) { received.push_back(io.data()); });
    waiter.ready();

    // published while the required client is absent: these must be held back, not sent
    for (int i = 0; i < 3; ++i)
    {
        IOData io;
        io.set_data("held-" + std::to_string(i));
        waiter.publish<held>(io);
    }

    // give a message that was wrongly sent every chance to come back before concluding it was held
    const auto hold_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (std::chrono::steady_clock::now() < hold_deadline)
        waiter.poll(std::chrono::milliseconds(100));

    assert(waiter.hold_state());
    assert(received.empty());
    std::cout << "hold engaged: nothing delivered while waiting for the required client"
              << std::endl;

    // the required client appears, but the hold lifts on its readiness rather than its existence
    goby::zenoh::InterProcessPortal<> helper(make_config("helper"));

    const auto unready_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
    while (std::chrono::steady_clock::now() < unready_deadline)
    {
        waiter.poll(std::chrono::milliseconds(100));
        helper.poll(std::chrono::milliseconds(0));
    }
    assert(waiter.hold_state());
    assert(received.empty());

    helper.ready();

    const auto release_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (received.size() < 3 && std::chrono::steady_clock::now() < release_deadline)
    {
        waiter.poll(std::chrono::milliseconds(100));
        helper.poll(std::chrono::milliseconds(0));
    }

    assert(!waiter.hold_state());
    std::cout << "hold released once the required client was ready" << std::endl;

    // everything published during the hold arrives, and in the order it was published
    assert(received.size() == 3);
    for (int i = 0; i < 3; ++i) assert(received.at(i) == "held-" + std::to_string(i));

    std::cout << "all tests passed" << std::endl;
    return 0;
}
