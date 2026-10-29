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

// Exercises PubSubTestRunner with an in-memory loopback endpoint: no transport, no daemon.
// Each case builds a PubSubTest, runs it, and checks the report.

#include <cassert>
#include <iostream>

#include <google/protobuf/text_format.h>

#include "dccl/dynamic_protobuf_manager.h"

#include "goby/middleware/protobuf/geographic.pb.h"
#include "goby/middleware/tool/pubsub_test.h"
#include "goby/util/debug_logger.h"

using namespace goby::middleware;
using namespace goby::middleware::tool;
using goby::glog;

// Delivers each publication back to the subscriber on the next poll(), like a broker would.
class LoopbackEndpoint : public PubSubTestEndpoint
{
  public:
    LoopbackEndpoint(std::string name) : name_(std::move(name)) {}

    const std::string& name() const override { return name_; }

    void publish(const protobuf::PubSubTestPublish& pub) override
    {
        PubSubTestMessage msg;
        msg.endpoint = name_;
        msg.group = pub.group();

        const std::string& spec = pub.type();
        const auto slash = spec.find('/');
        const std::string scheme = slash == std::string::npos ? "" : spec.substr(0, slash);
        const std::string type = slash == std::string::npos ? spec : spec.substr(slash + 1);

        if (scheme == "JSON" || spec == "JSON")
        {
            msg.scheme = MarshallingScheme::JSON;
            msg.type = (type.empty() || spec == "JSON") ? "nlohmann::json" : type;
            // the JSON scheme's wire format (BSON), as the real portals produce
            const auto bytes =
                SerializerParserHelper<nlohmann::json, MarshallingScheme::JSON>::serialize(
                    nlohmann::json::parse(pub.value()));
            msg.bytes.assign(bytes.begin(), bytes.end());
        }
        else
        {
            msg.scheme = MarshallingScheme::PROTOBUF;
            msg.type = type;
            auto pb = dccl::DynamicProtobufManager::new_protobuf_message<
                std::shared_ptr<google::protobuf::Message>>(type);
            const bool parsed =
                google::protobuf::TextFormat::ParseFromString(pub.value(), pb.get());
            assert(parsed);
            std::string serialized;
            pb->SerializeToString(&serialized);
            msg.bytes.assign(serialized.begin(), serialized.end());
        }
        queue_.push_back(msg);
    }

    void publish_health_request() override
    {
        // answer as if the applications in alive_ responded
        for (const auto& app : alive_)
        {
            protobuf::ProcessHealth health;
            health.set_name(app);
            health.mutable_main()->set_name(app);
            health.mutable_main()->set_state(protobuf::HEALTH__OK);
            PubSubTestMessage msg;
            msg.endpoint = name_;
            msg.group = std::string(groups::health_response);
            msg.scheme = MarshallingScheme::PROTOBUF;
            msg.type = protobuf::ProcessHealth::descriptor()->full_name();
            std::string serialized;
            health.SerializeToString(&serialized);
            msg.bytes.assign(serialized.begin(), serialized.end());
            queue_.push_back(msg);
        }
    }

    void subscribe_all(MessageHandler handler) override { handler_ = handler; }
    void ready() override {}
    bool hold_state() override { return false; }

    void poll(std::chrono::milliseconds wait) override
    {
        if (queue_.empty())
        {
            std::this_thread::sleep_for(wait);
            return;
        }
        auto pending = std::move(queue_);
        queue_.clear();
        for (const auto& msg : pending) handler_(msg);
    }

    void set_alive(std::vector<std::string> apps) { alive_ = std::move(apps); }

  private:
    std::string name_;
    std::vector<PubSubTestMessage> queue_;
    MessageHandler handler_;
    std::vector<std::string> alive_;
};

protobuf::PubSubTest parse_test(const std::string& text)
{
    protobuf::PubSubTest test;
    const bool parsed = google::protobuf::TextFormat::ParseFromString(text, &test);
    assert(parsed);
    return test;
}

protobuf::PubSubTestReport run(const protobuf::PubSubTest& test,
                               std::vector<std::string> alive = {})
{
    std::vector<std::unique_ptr<PubSubTestEndpoint>> endpoints;
    auto ep = std::make_unique<LoopbackEndpoint>("loop");
    ep->set_alive(alive);
    endpoints.push_back(std::move(ep));
    PubSubTestRunner runner(test, std::move(endpoints));
    auto report = runner.run();
    std::cout << PubSubTestRunner::render_human(report) << std::endl;
    return report;
}

void test_conditions()
{
    nlohmann::json j =
        nlohmann::json::parse(R"({"a": {"b": 2.5}, "list": [{"id": 7}], "s": "ON"})");
    std::string why;
    protobuf::PubSubTestCondition c;

    c.set_field("a.b");
    c.set_op(protobuf::PubSubTestCondition::GT);
    c.set_value("2");
    assert(tool::detail::evaluate_condition(c, j, why));
    c.set_op(protobuf::PubSubTestCondition::LT);
    assert(!tool::detail::evaluate_condition(c, j, why));

    c.set_field("list[0].id");
    c.set_op(protobuf::PubSubTestCondition::EQ);
    c.set_value("7");
    assert(tool::detail::evaluate_condition(c, j, why));

    c.set_field("s");
    c.set_op(protobuf::PubSubTestCondition::REGEX);
    c.set_value("O.");
    assert(tool::detail::evaluate_condition(c, j, why));

    c.set_field("missing");
    c.set_op(protobuf::PubSubTestCondition::ABSENT);
    assert(tool::detail::evaluate_condition(c, j, why));
    c.set_op(protobuf::PubSubTestCondition::EXISTS);
    assert(!tool::detail::evaluate_condition(c, j, why));

    // JSON subset
    assert(tool::detail::json_subset(nlohmann::json::parse(R"({"a": {"b": 2.5}})"), j, "", why));
    assert(!tool::detail::json_subset(nlohmann::json::parse(R"({"a": {"b": 3}})"), j, "", why));

    // Protobuf partial match
    protobuf::LatLonPoint actual, expected;
    actual.set_lat(42.5);
    actual.set_lon(-70.3);
    expected.set_lat(42.5);
    assert(tool::detail::protobuf_partial_match(expected, actual, "", why));
    expected.set_lon(0);
    assert(!tool::detail::protobuf_partial_match(expected, actual, "", why));
    assert(why.find("lon") != std::string::npos);
}

void test_pass_fail_counts()
{
    auto report = run(parse_test(R"(
        default_timeout: 0.3
        run {
            name: "match and condition"
            publish { group: "nav" type: "goby.middleware.protobuf.LatLonPoint" value: "lat: 1 lon: -2" }
            expect { group: "nav" type: "PROTOBUF/.*LatLonPoint" min_count: 1 match: "lat: 1"
                     condition { field: "lon" op: LT value: "0" } }
        }
        run {
            name: "rejected"
            publish { group: "nav" type: "goby.middleware.protobuf.LatLonPoint" value: "lat: 1 lon: 2" }
            expect { group: "nav" type: "PROTOBUF/.*" min_count: 1 match: "lat: 5" }
        }
        run {
            name: "exact count"
            publish { group: "j" type: "JSON/Thing" value: "{\"x\": 1}" repeat: 2 }
            expect { group: "j" type: "JSON/Thing" count: 2 match: "{\"x\": 1}" }
        }
        run {
            name: "too many"
            publish { group: "j" type: "JSON" value: "{}" repeat: 3 }
            expect { group: "j" type: "JSON/.*" count: 2 }
        }
        run {
            name: "nothing expected"
            expect { group: "quiet" count: 0 }
        }
    )"));

    assert(report.result() == protobuf::FAIL);
    assert(report.run_size() == 5);
    assert(report.run(0).result() == protobuf::PASS);
    assert(report.run(1).result() == protobuf::FAIL);
    assert(report.run(1).expect(0).rejected() == 1);
    assert(report.run(1).expect(0).last_rejected().find("lat") != std::string::npos);
    assert(report.run(2).result() == protobuf::PASS);
    assert(report.run(2).expect(0).received() == 2);
    assert(report.run(3).result() == protobuf::FAIL);
    assert(report.run(4).result() == protobuf::PASS);
}

void test_sequential_and_parallel()
{
    // two steps each holding for their timeout: parallel finishes in ~0.3 s, sequential in ~0.6 s
    const std::string steps = R"(
        step { name: "a" expect { group: "none" count: 0 timeout: 0.3 } }
        step { name: "b" expect { group: "none" count: 0 timeout: 0.3 } }
    )";
    auto parallel = run(parse_test("run { name: \"p\" mode: PARALLEL " + steps + " }"));
    auto sequential = run(parse_test("run { name: \"s\" mode: SEQUENTIAL " + steps + " }"));
    assert(parallel.result() == protobuf::PASS);
    assert(sequential.result() == protobuf::PASS);
    assert(parallel.run(0).step_size() == 2);
    assert(parallel.run(0).elapsed() < 0.5);
    assert(sequential.run(0).elapsed() > 0.55);
}

void test_require_app()
{
    auto test = parse_test(R"(
        require_app { name: "alpha" name: "beta" timeout: 1 request_interval: 0.05 }
        run { name: "noop" }
    )");
    auto ok = run(test, {"alpha", "beta"});
    assert(ok.result() == protobuf::PASS);
    assert(ok.require_app(0).result() == protobuf::PASS);

    auto missing = run(test, {"alpha"});
    assert(missing.result() == protobuf::ERROR);
    assert(missing.require_app(0).missing_size() == 1);
    assert(missing.require_app(0).missing(0) == "beta");
    assert(missing.run_size() == 0);
}

void test_json_render()
{
    auto report = run(parse_test("name: \"r\" run { name: \"noop\" }"));
    const std::string json = PubSubTestRunner::render_json(report);
    auto j = nlohmann::json::parse(json);
    assert(j["name"] == "r");
    assert(j["result"] == "PASS");
    assert(j["run"][0]["name"] == "noop");
}

int main(int argc, char* argv[])
{
    glog.add_stream(goby::util::logger::WARN, &std::cerr);
    glog.set_name(argv[0]);

    test_conditions();
    test_pass_fail_counts();
    test_sequential_and_parallel();
    test_require_app();
    test_json_render();

    std::cout << "all tests passed" << std::endl;
    return 0;
}
