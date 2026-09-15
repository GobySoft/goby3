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

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <deque>
#include <iostream>
#include <mutex>
#include <set>
#include <string>
#include <thread>

#include "goby/middleware/marshalling/dccl.h"
#include "goby/middleware/marshalling/protobuf.h"
#include "goby/middleware/transport/interthread.h"
#include "goby/middleware/transport/intervehicle.h"
#include "goby/util/debug_logger.h"

#include "goby/test/middleware/middleware_intervehicle_metadata/test.pb.h"

// Tests the DCCL metadata (the protobuf file descriptors of the published type) that an
// InterVehicleForwarder attaches to publications until the portal tells it that it has loaded
// that type:
//
//  1. Each .proto file is included exactly once and after its own dependencies, even when a
//     file (here google/protobuf/descriptor.proto) is imported along more than one path.
//  2. The metadata is attached at most once per TransporterConfig::metadata_interval, rather
//     than to every publication.
//  3. METADATA_EXCLUDE from the portal stops the metadata altogether.
//  4. METADATA_INCLUDE after an exclude (e.g. the portal restarted) attaches it to the very next
//     publication, while a repeated METADATA_INCLUDE does not reset the interval.
//
// The forwarder runs in its own thread, publishing Samples when asked. The main thread stands in
// for the portal: InterThreadTransporter delivers between threads, so it sees what the forwarder
// publishes to intervehicle::groups::modem_data_out and sends metadata requests back to it.

using goby::middleware::protobuf::SerializerMetadataRequest;
using goby::middleware::protobuf::SerializerTransporterMessage;
using goby::test::middleware::intervehicle_metadata::protobuf::Sample;

constexpr goby::middleware::Group broadcast{"broadcast", goby::middleware::Group::broadcast_group};

const auto metadata_interval = std::chrono::milliseconds(500);
const auto poll_interval = std::chrono::milliseconds(10);
const auto timeout = std::chrono::seconds(10);

std::atomic<int> publish_requested{0};
std::atomic<int> publish_completed{0};
std::atomic<bool> forwarder_done{false};

std::mutex captured_mutex;
std::deque<SerializerTransporterMessage> captured;

void check(bool condition, const std::string& what)
{
    if (!condition)
    {
        std::cerr << "Test failed: " << what << std::endl;
        std::exit(EXIT_FAILURE);
    }
    std::cout << "OK: " << what << std::endl;
}

void forwarder_thread()
{
    goby::middleware::InterThreadTransporter inner;
    goby::middleware::InterVehicleForwarder<goby::middleware::InterThreadTransporter> intervehicle(
        inner);

    goby::middleware::protobuf::TransporterConfig publisher_cfg;
    publisher_cfg.mutable_intervehicle()->set_metadata_interval_with_units(
        std::chrono::duration<double>(metadata_interval).count() * boost::units::si::seconds);
    goby::middleware::Publisher<Sample> publisher(publisher_cfg);

    while (!forwarder_done)
    {
        intervehicle.poll(poll_interval);

        if (publish_requested > publish_completed)
        {
            Sample sample;
            sample.set_a(1);
            intervehicle.publish<broadcast>(sample, publisher);
            ++publish_completed;
        }
    }
}

int main(int /*argc*/, char* argv[])
{
    goby::glog.add_stream(goby::util::logger::WARN, &std::cerr);
    goby::glog.set_name(argv[0]);

    goby::middleware::InterThreadTransporter portal;
    portal.subscribe<goby::middleware::intervehicle::groups::modem_data_out,
                     SerializerTransporterMessage>(
        [](const SerializerTransporterMessage& msg)
        {
            std::lock_guard<std::mutex> lock(captured_mutex);
            captured.push_back(msg);
        });

    std::thread forwarder(forwarder_thread);

    auto publish = [&]() -> SerializerTransporterMessage
    {
        {
            std::lock_guard<std::mutex> lock(captured_mutex);
            captured.clear();
        }
        ++publish_requested;

        auto deadline = std::chrono::steady_clock::now() + timeout;
        while (std::chrono::steady_clock::now() < deadline)
        {
            portal.poll(poll_interval);
            std::lock_guard<std::mutex> lock(captured_mutex);
            if (!captured.empty())
                return captured.back();
        }
        check(false, "forwarder handed the publication to the portal");
        return SerializerTransporterMessage();
    };

    // the forwarder acts on the request when it next polls
    auto request_metadata = [&](const SerializerTransporterMessage& from,
                                SerializerMetadataRequest::Request request)
    {
        SerializerMetadataRequest req;
        *req.mutable_key() = from.key();
        req.mutable_key()->clear_metadata();
        req.set_request(request);
        portal.publish<goby::middleware::intervehicle::groups::metadata_request>(req);
        std::this_thread::sleep_for(10 * poll_interval);
    };

    // 1. first publication carries the metadata, with each file once and after its dependencies
    auto first = publish();
    check(first.key().has_metadata(), "first publication carries metadata");

    std::set<std::string> seen;
    bool dependencies_first = true;
    for (const auto& file : first.key().metadata().file_descriptor())
    {
        check(!seen.count(file.name()), "file " + file.name() + " included once");
        for (const auto& dependency : file.dependency())
            dependencies_first = dependencies_first && seen.count(dependency);
        seen.insert(file.name());
    }
    check(dependencies_first, "each file follows its dependencies");
    check(seen.count("google/protobuf/descriptor.proto") &&
              seen.count("dccl/option_extensions.proto"),
          "shared dependency and its importer are both included");

    // 2. metadata_interval
    check(!publish().key().has_metadata(), "no metadata within metadata_interval");
    std::this_thread::sleep_for(metadata_interval + poll_interval);
    auto after_interval = publish();
    check(after_interval.key().has_metadata(), "metadata again after metadata_interval");

    // 3. the portal has the type
    request_metadata(after_interval, SerializerMetadataRequest::METADATA_EXCLUDE);
    std::this_thread::sleep_for(metadata_interval + poll_interval);
    auto after_exclude = publish();
    check(!after_exclude.key().has_metadata(), "no metadata after METADATA_EXCLUDE");

    // 4. the portal lost the type
    request_metadata(after_exclude, SerializerMetadataRequest::METADATA_INCLUDE);
    auto after_include = publish();
    check(after_include.key().has_metadata(), "metadata immediately after METADATA_INCLUDE");

    request_metadata(after_include, SerializerMetadataRequest::METADATA_INCLUDE);
    check(!publish().key().has_metadata(),
          "repeated METADATA_INCLUDE does not reset metadata_interval");

    forwarder_done = true;
    forwarder.join();

    std::cout << "All tests passed." << std::endl;
    return EXIT_SUCCESS;
}
