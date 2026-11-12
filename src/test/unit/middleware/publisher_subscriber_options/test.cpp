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
// along with Goby.  If not, see <http://www.gnu.org/licenses/>.

// Publisher::Options and Subscriber::Options (#375):
//  - both can be built with C++20 designated initializers, and carry every field through to the
//    Publisher/Subscriber (including the ack_required that an acked_func implies);
//  - group_in_message = false lets an intervehicle publish or subscribe use a non-broadcast group
//    without a set_group_func/group_func, which is otherwise an error;
//  - a Subscriber with group_in_message = false takes every received message of its type to be in
//    the subscribed group, while one with a group_func still filters by it.

#include <cstdlib>
#include <iostream>
#include <string>

#include "goby/middleware/marshalling/dccl.h"
#include "goby/middleware/transport/interthread.h"
#include "goby/middleware/transport/intervehicle.h"

#include "goby/test/unit/middleware/publisher_subscriber_options/test.pb.h"

using goby::middleware::Group;
using goby::middleware::Publisher;
using goby::middleware::Subscriber;
using goby::test::middleware::publisher_subscriber_options::protobuf::Sample;

constexpr Group group7{"group7", 7};

void check(bool condition, const std::string& what)
{
    if (!condition)
    {
        std::cerr << "Test failed: " << what << std::endl;
        std::exit(EXIT_FAILURE);
    }
}

void test_options()
{
    goby::middleware::protobuf::TransporterConfig cfg;
    cfg.mutable_intervehicle()->add_publisher_id(3);

    Publisher<Sample> publisher(
        {.cfg = cfg,
         .set_group_func = [](Sample& s, const Group& g) { s.set_group(g.numeric()); },
         .acked_func = [](const Sample&, const goby::middleware::intervehicle::protobuf::AckData&)
         {}});
    check(publisher.cfg().intervehicle().publisher_id(0) == 3, "Publisher Options cfg");
    check(publisher.has_set_group_func(), "Publisher Options set_group_func");
    check(bool(publisher.acked_func()), "Publisher Options acked_func");
    check(publisher.cfg().intervehicle().buffer().ack_required(),
          "Publisher Options acked_func implies ack_required");
    check(publisher.group_in_message(), "Publisher group_in_message defaults to true");
    Sample s;
    publisher.set_group(s, group7);
    check(s.group() == 7, "Publisher Options set_group_func is used");

    Subscriber<Sample> subscriber(
        {.cfg = cfg, .group_func = [](const Sample& s) { return Group(s.group()); }});
    check(subscriber.cfg().intervehicle().publisher_id(0) == 3, "Subscriber Options cfg");
    check(subscriber.group(s).numeric() == 7, "Subscriber Options group_func is used");
    check(subscriber.group_in_message(), "Subscriber group_in_message defaults to true");

    check(!Publisher<Sample>({.group_in_message = false}).group_in_message(),
          "Publisher Options group_in_message");
    check(!Subscriber<Sample>({.group_in_message = false}).group_in_message(),
          "Subscriber Options group_in_message");
}

// publishing or subscribing to group7 with no group function
void test_group_func_not_required()
{
    goby::middleware::InterThreadTransporter inner;
    goby::middleware::InterVehicleForwarder<goby::middleware::InterThreadTransporter> intervehicle(
        inner);

    Sample s;
    s.set_a(1);

    bool threw = false;
    try
    {
        intervehicle.publish_dynamic(s, group7, Publisher<Sample>());
    }
    catch (const goby::middleware::InvalidPublication&)
    {
        threw = true;
    }
    check(threw, "publishing to a non-broadcast group without set_group_func is an error");
    intervehicle.publish_dynamic(s, group7, Publisher<Sample>({.group_in_message = false}));

    threw = false;
    try
    {
        intervehicle.subscribe_dynamic<Sample>([](const Sample&) {}, group7, Subscriber<Sample>());
    }
    catch (const goby::middleware::InvalidSubscription&)
    {
        threw = true;
    }
    check(threw, "subscribing to a non-broadcast group without group_func is an error");
    intervehicle.subscribe_dynamic<Sample>([](const Sample&) {}, group7,
                                           Subscriber<Sample>({.group_in_message = false}));
}

// how many times a message is handed to a group7 subscription made with this Subscriber
int deliveries(const Subscriber<Sample>& subscriber)
{
    int count = 0;
    goby::middleware::IntervehicleSerializationSubscription<
        Sample, goby::middleware::MarshallingScheme::DCCL>
        subscription([&count](std::shared_ptr<const Sample>) { ++count; }, group7, subscriber);

    Sample s;
    s.set_a(2); // group not set: it isn't stored in the message
    auto bytes = goby::middleware::SerializerParserHelper<
        Sample, goby::middleware::MarshallingScheme::DCCL>::serialize(s);
    subscription.post(bytes.data(), bytes.data() + bytes.size(),
                      goby::middleware::intervehicle::protobuf::Header());
    return count;
}

void test_delivery()
{
    check(deliveries(Subscriber<Sample>({.group_in_message = false})) == 1,
          "group_in_message = false: a received message is in the subscribed group");
    check(deliveries(Subscriber<Sample>({.group_func = [](const Sample&) { return Group(8); }})) ==
              0,
          "group_func still filters: a message in group 8 is not delivered to group 7");
    check(deliveries(Subscriber<Sample>({.group_func = [](const Sample&) { return Group(7); }})) ==
              1,
          "group_func still filters: a message in group 7 is delivered to group 7");
}

int main()
{
    test_options();
    test_group_func_not_required();
    test_delivery();
    std::cout << "all tests passed" << std::endl;
    return 0;
}
