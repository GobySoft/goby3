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

#ifndef GOBY_MIDDLEWARE_TOOL_PUBSUB_TEST_H
#define GOBY_MIDDLEWARE_TOOL_PUBSUB_TEST_H

#include <chrono>
#include <cstdlib>
#include <functional>
#include <iomanip>
#include <map>
#include <memory>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include <google/protobuf/text_format.h>
#include <google/protobuf/util/json_util.h>

#include <boost/units/systems/si.hpp>

#include "goby/exception.h"
#include "goby/middleware/coroner/groups.h"
#include "goby/middleware/log/dccl_log_plugin.h"
#include "goby/middleware/log/json_log_plugin.h"
#include "goby/middleware/log/log_entry.h"
#include "goby/middleware/log/protobuf_log_plugin.h"
#include "goby/middleware/marshalling/interface.h"
#include "goby/middleware/marshalling/json.h"
#include "goby/middleware/marshalling/protobuf.h"
#include "goby/middleware/protobuf/coroner.pb.h"
#include "goby/middleware/protobuf/pubsub_test.pb.h"
#include "goby/middleware/tool/publish_subscribe_tool.h"
#include "goby/util/debug_logger.h"

namespace goby
{
namespace middleware
{
namespace tool
{
/// \brief A raw message received on an endpoint
struct PubSubTestMessage
{
    std::string endpoint;
    std::vector<unsigned char> bytes;
    int scheme;
    std::string type;
    std::string group;
};

/// \brief Type-erased connection to one layer (interprocess or intermodule) of one transport
class PubSubTestEndpoint
{
  public:
    using Clock = std::chrono::steady_clock;
    using MessageHandler = std::function<void(const PubSubTestMessage&)>;

    virtual ~PubSubTestEndpoint() = default;

    virtual const std::string& name() const = 0;
    virtual void publish(const protobuf::PubSubTestPublish& pub) = 0;
    virtual void publish_health_request() = 0;
    virtual void subscribe_all(MessageHandler handler) = 0;
    virtual void ready() = 0;
    virtual bool hold_state() = 0;
    virtual void poll(std::chrono::milliseconds wait) = 0;
};

/// \brief PubSubTestEndpoint over any InterProcessPortalImplementation (interprocess or intermodule tag)
template <typename Portal, typename PortalConfig>
class PubSubTestPortalEndpoint : public PubSubTestEndpoint
{
  public:
    PubSubTestPortalEndpoint(std::string name, const PortalConfig& cfg, std::string tool_name)
        : name_(std::move(name)), tool_name_(std::move(tool_name)), portal_(cfg)
    {
    }

    const std::string& name() const override { return name_; }

    void publish(const protobuf::PubSubTestPublish& pub) override
    {
        publish_tool_impl(portal_, pub, tool_name_);
    }

    void publish_health_request() override
    {
        portal_.template publish<groups::health_request>(protobuf::HealthRequest());
    }

    void subscribe_all(MessageHandler handler) override
    {
        portal_.subscribe_regex([this, handler](const std::vector<unsigned char>& bytes, int scheme,
                                                const std::string& type, const Group& group)
                                { handler({name_, bytes, scheme, type, std::string(group)}); },
                                {MarshallingScheme::ALL_SCHEMES}, ".*", ".*");
    }

    void ready() override { portal_.ready(); }
    bool hold_state() override { return portal_.hold_state(); }
    void poll(std::chrono::milliseconds wait) override { portal_.poll(wait); }

  private:
    std::string name_;
    std::string tool_name_;
    Portal portal_;
};

namespace detail
{
inline std::string json_scalar_to_string(const nlohmann::json& j)
{
    return j.is_string() ? j.get<std::string>() : j.dump();
}

inline bool json_is_numeric(const nlohmann::json& j, double& d)
{
    if (j.is_number())
    {
        d = j.get<double>();
        return true;
    }
    if (j.is_string())
    {
        const std::string s = j.get<std::string>();
        char* end = nullptr;
        d = std::strtod(s.c_str(), &end);
        return !s.empty() && end && *end == '\0';
    }
    return false;
}

/// Resolve a dotted path with optional [n] indices, e.g. "pos.lat" or "wp[0].id"
inline const nlohmann::json* json_lookup(const nlohmann::json& root, const std::string& path)
{
    static const std::regex token_regex("([^.\\[\\]]+)|\\[([0-9]+)\\]");
    const nlohmann::json* cur = &root;
    for (auto it = std::sregex_iterator(path.begin(), path.end(), token_regex);
         it != std::sregex_iterator(); ++it)
    {
        if ((*it)[1].matched)
        {
            if (!cur->is_object() || !cur->contains((*it)[1].str()))
                return nullptr;
            cur = &(*cur)[(*it)[1].str()];
        }
        else
        {
            const auto idx = std::stoul((*it)[2].str());
            if (!cur->is_array() || idx >= cur->size())
                return nullptr;
            cur = &(*cur)[idx];
        }
    }
    return cur;
}

/// Every value in `expected` must be present and equal in `actual`
inline bool json_subset(const nlohmann::json& expected, const nlohmann::json& actual,
                        const std::string& path, std::string& why)
{
    if (expected.is_object())
    {
        if (!actual.is_object())
        {
            why = path + ": expected an object";
            return false;
        }
        for (auto it = expected.begin(); it != expected.end(); ++it)
        {
            const std::string child = path.empty() ? it.key() : path + "." + it.key();
            if (!actual.contains(it.key()))
            {
                why = child + ": missing";
                return false;
            }
            if (!json_subset(it.value(), actual[it.key()], child, why))
                return false;
        }
        return true;
    }

    double a, b;
    const bool equal = (json_is_numeric(expected, a) && json_is_numeric(actual, b))
                           ? a == b
                           : json_scalar_to_string(expected) == json_scalar_to_string(actual);
    if (!equal)
        why = path + ": expected " + expected.dump() + ", got " + actual.dump();
    return equal;
}

inline bool evaluate_condition(const protobuf::PubSubTestCondition& cond, const nlohmann::json& msg,
                               std::string& why)
{
    using Op = protobuf::PubSubTestCondition;
    const nlohmann::json* value = json_lookup(msg, cond.field());

    if (cond.op() == Op::EXISTS || cond.op() == Op::ABSENT)
    {
        const bool exists = value != nullptr;
        if (exists != (cond.op() == Op::EXISTS))
            why = cond.field() + (exists ? ": present" : ": absent");
        return exists == (cond.op() == Op::EXISTS);
    }

    if (!value)
    {
        why = cond.field() + ": absent";
        return false;
    }

    const std::string actual = json_scalar_to_string(*value);
    bool ok = false;
    switch (cond.op())
    {
        case Op::REGEX: ok = std::regex_match(actual, std::regex(cond.value())); break;
        case Op::EXISTS:
        case Op::ABSENT: break;
        default:
        {
            double a, b;
            const bool numeric =
                json_is_numeric(*value, a) && json_is_numeric(nlohmann::json(cond.value()), b);
            const int cmp = numeric ? (a < b ? -1 : (a > b ? 1 : 0)) : actual.compare(cond.value());
            switch (cond.op())
            {
                case Op::EQ: ok = cmp == 0; break;
                case Op::NE: ok = cmp != 0; break;
                case Op::LT: ok = cmp < 0; break;
                case Op::LE: ok = cmp <= 0; break;
                case Op::GT: ok = cmp > 0; break;
                case Op::GE: ok = cmp >= 0; break;
                default: break;
            }
        }
    }
    if (!ok)
        why = cond.field() + " " + Op::Op_Name(cond.op()) + " " + cond.value() + ": got " + actual;
    return ok;
}

/// Every field set in `expected` must equal its counterpart in `actual`
inline bool protobuf_partial_match(const google::protobuf::Message& expected,
                                   const google::protobuf::Message& actual, const std::string& path,
                                   std::string& why)
{
    using google::protobuf::FieldDescriptor;
    const auto* refl = expected.GetReflection();
    const auto* actual_refl = actual.GetReflection();
    std::vector<const FieldDescriptor*> fields;
    refl->ListFields(expected, &fields);

    for (const auto* field : fields)
    {
        const std::string child = path.empty() ? field->name() : path + "." + field->name();
        if (field->is_repeated())
        {
            const int n = refl->FieldSize(expected, field);
            if (actual_refl->FieldSize(actual, field) != n)
            {
                why = child + ": expected " + std::to_string(n) + " elements, got " +
                      std::to_string(actual_refl->FieldSize(actual, field));
                return false;
            }
            for (int i = 0; i < n; ++i)
            {
                const std::string elem = child + "[" + std::to_string(i) + "]";
                if (field->cpp_type() == FieldDescriptor::CPPTYPE_MESSAGE)
                {
                    if (!protobuf_partial_match(refl->GetRepeatedMessage(expected, field, i),
                                                actual_refl->GetRepeatedMessage(actual, field, i),
                                                elem, why))
                        return false;
                }
                else
                {
                    std::string e, a;
                    google::protobuf::TextFormat::PrintFieldValueToString(expected, field, i, &e);
                    google::protobuf::TextFormat::PrintFieldValueToString(actual, field, i, &a);
                    if (e != a)
                    {
                        why = elem + ": expected " + e + ", got " + a;
                        return false;
                    }
                }
            }
        }
        else if (field->cpp_type() == FieldDescriptor::CPPTYPE_MESSAGE)
        {
            if (!actual_refl->HasField(actual, field))
            {
                why = child + ": missing";
                return false;
            }
            if (!protobuf_partial_match(refl->GetMessage(expected, field),
                                        actual_refl->GetMessage(actual, field), child, why))
                return false;
        }
        else
        {
            if (!actual_refl->HasField(actual, field))
            {
                why = child + ": missing";
                return false;
            }
            std::string e, a;
            google::protobuf::TextFormat::PrintFieldValueToString(expected, field, -1, &e);
            google::protobuf::TextFormat::PrintFieldValueToString(actual, field, -1, &a);
            if (e != a)
            {
                why = child + ": expected " + e + ", got " + a;
                return false;
            }
        }
    }
    return true;
}

inline double seconds_since(PubSubTestEndpoint::Clock::time_point start)
{
    return std::chrono::duration<double>(PubSubTestEndpoint::Clock::now() - start).count();
}

inline PubSubTestEndpoint::Clock::duration to_duration(double seconds)
{
    return std::chrono::duration_cast<PubSubTestEndpoint::Clock::duration>(
        std::chrono::duration<double>(seconds));
}

/// Split "SCHEME/type_regex" into a scheme set and the type regex
inline void parse_type_spec(const protobuf::PubSubTestExpect& expect, std::set<int>& schemes,
                            std::string& type_regex)
{
    const std::string& spec = expect.type();
    const auto slash = spec.find('/');
    if (slash != std::string::npos && slash > 0)
    {
        schemes = {MarshallingScheme::from_string(spec.substr(0, slash))};
        type_regex = spec.substr(slash + 1);
    }
    else
    {
        schemes = expect.has_scheme()
                      ? std::set<int>{MarshallingScheme::from_string(expect.scheme())}
                      : std::set<int>{MarshallingScheme::ALL_SCHEMES};
        type_regex = spec;
    }
}
} // namespace detail

/// \brief Runs a PubSubTest over a set of endpoints on a single thread
class PubSubTestRunner
{
  public:
    using Clock = PubSubTestEndpoint::Clock;

    PubSubTestRunner(const protobuf::PubSubTest& test,
                     std::vector<std::unique_ptr<PubSubTestEndpoint>> endpoints)
        : test_(test), endpoints_(std::move(endpoints))
    {
        if (endpoints_.empty())
            throw goby::Exception("at least one endpoint is required");

        for (auto& ep : endpoints_)
        {
            if (!endpoint_index_.insert({ep->name(), ep.get()}).second)
                throw goby::Exception("duplicate endpoint name: " + ep->name());
        }
        validate();
    }

    protobuf::PubSubTestReport run()
    {
        protobuf::PubSubTestReport report;
        report.set_name(test_.name());
        const auto start = Clock::now();

        try
        {
            connect();
            for (auto& ep : endpoints_)
                ep->subscribe_all([this](const PubSubTestMessage& msg) { on_message(msg); });

            bool ok = true;
            for (const auto& req : test_.require_app())
                ok = run_require_app(req, *report.add_require_app()) && ok;

            if (!ok)
            {
                report.set_result(protobuf::ERROR);
                report.set_error("required applications did not respond");
            }
            else
            {
                run_all(report);
            }
        }
        catch (const std::exception& e)
        {
            report.set_result(protobuf::ERROR);
            report.set_error(e.what());
        }

        report.set_elapsed_with_units(detail::seconds_since(start) * boost::units::si::seconds);
        return report;
    }

    static std::string render_human(const protobuf::PubSubTestReport& report)
    {
        std::stringstream ss;
        ss << std::fixed << std::setprecision(2);
        ss << "Test: " << report.name() << " ... " << result_name(report.result()) << " ("
           << report.elapsed() << " s)\n";
        if (report.has_error())
            ss << "  error: " << report.error() << "\n";

        for (const auto& req : report.require_app())
        {
            ss << "  require_app [" << req.endpoint() << "] ... " << result_name(req.result())
               << " (" << req.elapsed() << " s" << (req.from_coroner() ? ", via goby_coroner" : "")
               << ")";
            if (!req.missing().empty())
                ss << " missing: " << join(req.missing());
            if (!req.unhealthy().empty())
                ss << " unhealthy: " << join(req.unhealthy());
            ss << "\n";
        }

        for (int i = 0; i < report.run_size(); ++i)
            render_run(ss, report.run(i), std::to_string(i + 1));
        return ss.str();
    }

    static std::string render_json(const protobuf::PubSubTestReport& report)
    {
        std::string out;
        google::protobuf::util::JsonPrintOptions opts;
        opts.add_whitespace = true;
        opts.preserve_proto_field_names = true;
        google::protobuf::util::MessageToJsonString(report, &out, opts);
        return out;
    }

  private:
    struct ExpectState
    {
        const protobuf::PubSubTestExpect* cfg;
        PubSubTestEndpoint* endpoint;
        std::set<int> schemes;
        std::regex type_regex;
        std::regex group_regex;
        unsigned min_count;
        unsigned max_count;
        bool has_max;
        bool exact; // hold until the deadline so that extra messages are seen
        Clock::time_point deadline;
        unsigned received{0};
        unsigned rejected{0};
        std::string last_rejected;
        bool resolved{false};
        bool passed{false};
        std::string reason;
    };

    struct PublishState
    {
        const protobuf::PubSubTestPublish* cfg;
        PubSubTestEndpoint* endpoint;
        Clock::time_point next;
        unsigned remaining;
    };

    /// A run, a step, or the synthetic root; leaves hold publishes and expectations
    struct Node
    {
        std::string name;
        protobuf::PubSubTestRunMode mode;
        const google::protobuf::RepeatedPtrField<protobuf::PubSubTestPublish>* publish_cfg;
        const google::protobuf::RepeatedPtrField<protobuf::PubSubTestExpect>* expect_cfg;
        double timeout;
        bool is_run{false};
        bool started{false};
        bool done{false};
        protobuf::PubSubTestResult result{protobuf::PASS};
        Clock::time_point start;
        double elapsed{0};
        std::vector<Node> children;
        std::size_t next_child{0};
        std::vector<ExpectState> expects;
        std::vector<PublishState> publishes;
        std::vector<unsigned> publish_counts;
    };

    void validate()
    {
        for (const auto& req : test_.require_app()) endpoint(req.endpoint());
        for (const auto& run : test_.run())
        {
            if (run.step_size() > 0 && (run.publish_size() > 0 || run.expect_size() > 0))
                throw goby::Exception("run '" + run.name() +
                                      "': a run has either steps or publish/expect entries, "
                                      "not both");
            validate_leaf(run.name(), run.publish(), run.expect());
            for (const auto& step : run.step())
                validate_leaf(run.name() + "/" + step.name(), step.publish(), step.expect());
        }
    }

    void validate_leaf(const std::string& name,
                       const google::protobuf::RepeatedPtrField<protobuf::PubSubTestPublish>& pubs,
                       const google::protobuf::RepeatedPtrField<protobuf::PubSubTestExpect>& exps)
    {
        for (const auto& pub : pubs) endpoint(pub.endpoint());
        for (const auto& exp : exps)
        {
            endpoint(exp.endpoint());
            std::set<int> schemes;
            std::string type_regex;
            detail::parse_type_spec(exp, schemes, type_regex);
            try
            {
                std::regex type_check(type_regex);
                std::regex group_check(exp.group());
            }
            catch (const std::regex_error& e)
            {
                throw goby::Exception("'" + name + "': invalid regex: " + e.what());
            }
        }
    }

    PubSubTestEndpoint* endpoint(const std::string& name)
    {
        if (name.empty())
            return endpoints_.front().get();
        auto it = endpoint_index_.find(name);
        if (it == endpoint_index_.end())
            throw goby::Exception("unknown endpoint '" + name + "'");
        return it->second;
    }

    void poll_all()
    {
        for (auto& ep : endpoints_) ep->poll(std::chrono::milliseconds(2));
    }

    void connect()
    {
        for (auto& ep : endpoints_) ep->ready();
        const auto deadline = Clock::now() + detail::to_duration(test_.connect_timeout());
        for (;;)
        {
            bool holding = false;
            for (auto& ep : endpoints_) holding = holding || ep->hold_state();
            if (!holding)
                return;
            if (Clock::now() > deadline)
                throw goby::Exception("endpoints did not connect within " +
                                      std::to_string(test_.connect_timeout()) + " s");
            poll_all();
        }
    }

    bool run_require_app(const protobuf::PubSubTestRequireApp& req,
                         protobuf::PubSubTestRequireAppReport& report)
    {
        auto* ep = endpoint(req.endpoint());
        report.set_endpoint(ep->name());
        for (const auto& n : req.name()) report.add_name(n);

        health_.clear();
        coroner_seen_ = false;
        const auto start = Clock::now();
        const auto deadline = start + detail::to_duration(req.timeout());
        const auto interval = detail::to_duration(req.request_interval());
        auto next_request = start;

        auto satisfied = [&](std::vector<std::string>& missing, std::vector<std::string>& unhealthy)
        {
            missing.clear();
            unhealthy.clear();
            for (const auto& n : req.name())
            {
                auto it = health_.find(n);
                if (it == health_.end())
                    missing.push_back(n);
                else if (req.require_ok() && it->second != protobuf::HEALTH__OK)
                    unhealthy.push_back(n);
            }
            return missing.empty() && unhealthy.empty();
        };

        std::vector<std::string> missing, unhealthy;
        for (;;)
        {
            if (satisfied(missing, unhealthy))
                break;
            const auto now = Clock::now();
            if (now > deadline)
                break;
            if (!coroner_seen_ && now >= next_request)
            {
                ep->publish_health_request();
                next_request = now + interval;
            }
            poll_all();
        }

        const bool ok = satisfied(missing, unhealthy);
        report.set_result(ok ? protobuf::PASS : protobuf::ERROR);
        report.set_elapsed_with_units(detail::seconds_since(start) * boost::units::si::seconds);
        report.set_from_coroner(coroner_seen_);
        for (const auto& n : missing) report.add_missing(n);
        for (const auto& n : unhealthy) report.add_unhealthy(n);
        return ok;
    }

    void run_all(protobuf::PubSubTestReport& report)
    {
        Node root;
        root.mode = test_.mode();
        root.publish_cfg = nullptr;
        root.expect_cfg = nullptr;
        root.timeout = test_.default_timeout();
        for (const auto& run : test_.run())
        {
            Node b;
            b.name = run.name();
            b.mode = run.mode();
            b.publish_cfg = &run.publish();
            b.expect_cfg = &run.expect();
            b.timeout = run.has_timeout() ? run.timeout() : root.timeout;
            b.is_run = true;
            for (const auto& step : run.step())
            {
                Node st;
                st.name = step.name();
                st.mode = protobuf::SEQUENTIAL;
                st.publish_cfg = &step.publish();
                st.expect_cfg = &step.expect();
                st.timeout = step.has_timeout() ? step.timeout() : b.timeout;
                b.children.push_back(std::move(st));
            }
            root.children.push_back(std::move(b));
        }

        start_node(root);
        while (!root.done)
        {
            tick_node(root);
            if (!root.done)
                poll_all();
        }

        for (const auto& b : root.children)
        {
            auto& br = *report.add_run();
            br.set_name(b.name);
            br.set_mode(b.mode);
            br.set_result(b.result);
            br.set_elapsed_with_units(b.elapsed * boost::units::si::seconds);
            write_leaf_report(b, *br.mutable_publish(), *br.mutable_expect());
            for (const auto& st : b.children)
            {
                auto& sr = *br.add_step();
                sr.set_name(st.name);
                sr.set_result(st.result);
                sr.set_elapsed_with_units(st.elapsed * boost::units::si::seconds);
                write_leaf_report(st, *sr.mutable_publish(), *sr.mutable_expect());
            }
        }
        report.set_result(root.result);
    }

    void
    write_leaf_report(const Node& n,
                      google::protobuf::RepeatedPtrField<protobuf::PubSubTestPublishReport>& pubs,
                      google::protobuf::RepeatedPtrField<protobuf::PubSubTestExpectReport>& exps)
    {
        for (std::size_t i = 0; i < n.publishes.size(); ++i)
        {
            auto& pr = *pubs.Add();
            pr.set_endpoint(n.publishes[i].endpoint->name());
            pr.set_group(n.publishes[i].cfg->group());
            pr.set_type(n.publishes[i].cfg->type());
            pr.set_count(n.publish_counts[i]);
        }
        for (const auto& e : n.expects) write_expect_report(e, *exps.Add());
    }

    void start_node(Node& s)
    {
        s.started = true;
        s.start = Clock::now();

        if (!s.children.empty())
        {
            if (s.mode == protobuf::PARALLEL)
                for (auto& child : s.children) start_node(child);
            else
                start_node(s.children[s.next_child]);
            return;
        }

        if (s.expect_cfg)
        {
            for (const auto& exp : *s.expect_cfg)
            {
                ExpectState e;
                e.cfg = &exp;
                e.endpoint = endpoint(exp.endpoint());
                std::string type_regex;
                detail::parse_type_spec(exp, e.schemes, type_regex);
                e.type_regex = std::regex(type_regex);
                e.group_regex = std::regex(exp.group());
                e.exact = exp.has_count();
                e.min_count = e.exact ? exp.count() : exp.min_count();
                e.has_max = e.exact || exp.has_max_count();
                e.max_count = e.exact ? exp.count() : exp.max_count();
                e.deadline =
                    s.start + detail::to_duration(exp.has_timeout() ? exp.timeout() : s.timeout);
                s.expects.push_back(std::move(e));
            }
        }

        if (s.publish_cfg)
        {
            for (const auto& pub : *s.publish_cfg)
            {
                PublishState p;
                p.cfg = &pub;
                p.endpoint = endpoint(pub.endpoint());
                p.next = s.start + detail::to_duration(pub.delay());
                p.remaining = pub.repeat();
                s.publishes.push_back(p);
                s.publish_counts.push_back(0);
            }
        }
        active_leaves_.insert(&s);
    }

    void finish_node(Node& s, protobuf::PubSubTestResult result)
    {
        s.done = true;
        active_leaves_.erase(&s);
        s.result = result;
        s.elapsed = detail::seconds_since(s.start);
    }

    void tick_node(Node& s)
    {
        if (s.done || !s.started)
            return;

        if (!s.children.empty())
        {
            bool all_done = true, any_fail = false;
            for (auto& child : s.children)
            {
                if (child.started)
                    tick_node(child);
                if (child.done)
                    any_fail = any_fail || child.result != protobuf::PASS;
                else
                    all_done = false;
            }
            if (s.mode == protobuf::SEQUENTIAL && s.children[s.next_child].done &&
                s.next_child + 1 < s.children.size())
            {
                start_node(s.children[++s.next_child]);
                all_done = false;
            }
            if (all_done)
                finish_node(s, any_fail ? protobuf::FAIL : protobuf::PASS);
            return;
        }

        const auto now = Clock::now();
        bool publishing = false;
        for (std::size_t i = 0; i < s.publishes.size(); ++i)
        {
            auto& p = s.publishes[i];
            while (p.remaining > 0 && now >= p.next)
            {
                p.endpoint->publish(*p.cfg);
                --p.remaining;
                p.next += detail::to_duration(p.cfg->interval());
                ++s.publish_counts[i];
            }
            publishing = publishing || p.remaining > 0;
        }

        bool all_resolved = true, any_fail = false;
        for (auto& e : s.expects)
        {
            if (!e.resolved)
            {
                const bool timed_out = now > e.deadline;
                if (e.has_max && e.received > e.max_count)
                    resolve(e, false, "received more than " + std::to_string(e.max_count));
                else if (!e.exact && e.received >= e.min_count)
                    resolve(e, true, "");
                else if (timed_out && e.received >= e.min_count)
                    resolve(e, true, "");
                else if (timed_out)
                    resolve(e, false,
                            "received " + std::to_string(e.received) + " of " +
                                std::to_string(e.min_count) + " before timeout");
            }
            all_resolved = all_resolved && e.resolved;
            any_fail = any_fail || (e.resolved && !e.passed);
        }

        if (all_resolved && !publishing)
            finish_node(s, any_fail ? protobuf::FAIL : protobuf::PASS);
    }

    void resolve(ExpectState& e, bool passed, const std::string& reason)
    {
        e.resolved = true;
        e.passed = passed;
        e.reason = reason;
    }

    void write_expect_report(const ExpectState& e, protobuf::PubSubTestExpectReport& r)
    {
        r.set_endpoint(e.endpoint->name());
        r.set_group(e.cfg->group());
        r.set_type(e.cfg->type());
        r.set_result(e.passed ? protobuf::PASS : protobuf::FAIL);
        r.set_received(e.received);
        r.set_rejected(e.rejected);
        r.set_min_count(e.min_count);
        if (e.has_max)
            r.set_max_count(e.max_count);
        if (!e.reason.empty())
            r.set_reason(e.reason);
        if (!e.last_rejected.empty())
            r.set_last_rejected(e.last_rejected);
    }

    void on_message(const PubSubTestMessage& msg)
    {
        if (msg.scheme == MarshallingScheme::PROTOBUF)
        {
            if (msg.group == std::string(groups::health_response))
            {
                protobuf::ProcessHealth health;
                if (health.ParseFromArray(msg.bytes.data(), msg.bytes.size()))
                    health_[health.name()] = health.main().state();
                return;
            }
            else if (msg.group == std::string(groups::health_report))
            {
                protobuf::VehicleHealth report;
                if (report.ParseFromArray(msg.bytes.data(), msg.bytes.size()))
                {
                    coroner_seen_ = true;
                    for (const auto& process : report.process())
                    {
                        // the coroner lists expected-but-dead processes too
                        if (process.main().error() == protobuf::ERROR__PROCESS_DIED)
                            health_.erase(process.name());
                        else
                            health_[process.name()] = process.main().state();
                    }
                }
                return;
            }
        }

        for (auto* block : active_leaves_)
        {
            for (auto& e : block->expects)
            {
                if (e.resolved || e.endpoint->name() != msg.endpoint)
                    continue;
                if (!e.schemes.count(MarshallingScheme::ALL_SCHEMES) &&
                    !e.schemes.count(msg.scheme))
                    continue;
                if (!std::regex_match(msg.type, e.type_regex) ||
                    !std::regex_match(msg.group, e.group_regex))
                    continue;

                std::string why;
                if (matches(e, msg, why))
                {
                    ++e.received;
                }
                else
                {
                    ++e.rejected;
                    e.last_rejected = why;
                }
            }
        }
    }

    bool matches(const ExpectState& e, const PubSubTestMessage& msg, std::string& why)
    {
        if (!e.cfg->has_match() && e.cfg->condition_size() == 0)
            return true;

        log::LogEntry entry(msg.bytes, msg.scheme, msg.type, DynamicGroup(msg.group));
        nlohmann::json json;
        try
        {
            if (msg.scheme == MarshallingScheme::PROTOBUF || msg.scheme == MarshallingScheme::DCCL)
            {
                auto msgs = msg.scheme == MarshallingScheme::DCCL
                                ? dccl_plugin_.parse_message(entry)
                                : protobuf_plugin_.parse_message(entry);
                if (msgs.empty())
                {
                    why = "could not decode message";
                    return false;
                }
                const auto& pb = *msgs.front();

                if (e.cfg->has_match())
                {
                    std::unique_ptr<google::protobuf::Message> expected(pb.New());
                    google::protobuf::TextFormat::Parser parser;
                    parser.AllowPartialMessage(true);
                    if (!parser.ParseFromString(e.cfg->match(), expected.get()))
                    {
                        why = "invalid 'match' for " + pb.GetDescriptor()->full_name();
                        return false;
                    }
                    if (!detail::protobuf_partial_match(*expected, pb, "", why))
                        return false;
                }

                if (e.cfg->condition_size() > 0)
                {
                    std::string jstr;
                    google::protobuf::util::JsonPrintOptions opts;
                    opts.preserve_proto_field_names = true;
                    google::protobuf::util::MessageToJsonString(pb, &jstr, opts);
                    json = nlohmann::json::parse(jstr);
                }
            }
            else if (msg.scheme == MarshallingScheme::JSON)
            {
                json = *json_plugin_.parse_message(entry);
                if (e.cfg->has_match() &&
                    !detail::json_subset(nlohmann::json::parse(e.cfg->match()), json, "", why))
                    return false;
            }
            else
            {
                why =
                    "cannot inspect contents of scheme " + MarshallingScheme::to_string(msg.scheme);
                return false;
            }
        }
        catch (const std::exception& ex)
        {
            why = std::string("could not decode message: ") + ex.what();
            return false;
        }

        for (const auto& cond : e.cfg->condition())
            if (!detail::evaluate_condition(cond, json, why))
                return false;
        return true;
    }

    static std::string result_name(protobuf::PubSubTestResult r)
    {
        return protobuf::PubSubTestResult_Name(r);
    }

    static std::string join(const google::protobuf::RepeatedPtrField<std::string>& v)
    {
        std::string out;
        for (const auto& s : v) out += (out.empty() ? "" : ", ") + s;
        return out;
    }

    static void
    render_leaf(std::stringstream& ss, const std::string& indent,
                const google::protobuf::RepeatedPtrField<protobuf::PubSubTestPublishReport>& pubs,
                const google::protobuf::RepeatedPtrField<protobuf::PubSubTestExpectReport>& exps)
    {
        for (const auto& p : pubs)
            ss << indent << "publish [" << p.endpoint() << "] " << p.group() << " " << p.type()
               << ": sent " << p.count() << "\n";
        for (const auto& e : exps)
        {
            ss << indent << "expect [" << e.endpoint() << "] " << e.group() << " " << e.type()
               << ": " << result_name(e.result()) << ", " << e.received() << " received";
            if (e.rejected() > 0)
                ss << ", " << e.rejected() << " rejected";
            if (e.has_reason())
                ss << ", " << e.reason();
            if (e.has_last_rejected())
                ss << " (last rejected: " << e.last_rejected() << ")";
            ss << "\n";
        }
    }

    static void render_run(std::stringstream& ss, const protobuf::PubSubTestRunReport& b,
                           const std::string& index)
    {
        ss << "  [" << index << "] " << b.name();
        if (b.step_size() > 0)
            ss << " (" << (b.mode() == protobuf::PARALLEL ? "parallel" : "sequential") << ")";
        ss << " ... " << result_name(b.result()) << " (" << b.elapsed() << " s)\n";
        render_leaf(ss, "      ", b.publish(), b.expect());
        for (int i = 0; i < b.step_size(); ++i)
        {
            const auto& st = b.step(i);
            ss << "    [" << index << "." << i + 1 << "] " << st.name() << " ... "
               << result_name(st.result()) << " (" << st.elapsed() << " s)\n";
            render_leaf(ss, "        ", st.publish(), st.expect());
        }
    }

  private:
    const protobuf::PubSubTest& test_;
    std::vector<std::unique_ptr<PubSubTestEndpoint>> endpoints_;
    std::map<std::string, PubSubTestEndpoint*> endpoint_index_;
    log::ProtobufPlugin protobuf_plugin_;
    log::DCCLPlugin dccl_plugin_;
    log::JSONPlugin json_plugin_;
    std::set<Node*> active_leaves_;

    std::map<std::string, protobuf::HealthState> health_;
    bool coroner_seen_{false};
};

} // namespace tool
} // namespace middleware
} // namespace goby

#endif
