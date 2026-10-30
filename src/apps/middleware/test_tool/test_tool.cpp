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

#include <algorithm>
#include <atomic>
#include <csignal>
#include <fstream>
#include <iostream>

#include <boost/units/systems/si.hpp>

#include "goby/middleware/application/configuration_reader.h"
#include "goby/middleware/application/interface.h"
#include "goby/middleware/application/tool.h"
#include "goby/middleware/tool/pubsub_test.h"
#include "goby/udpm/transport/intermodule.h"
#include "goby/udpm/transport/interprocess.h"

#ifdef GOBY_TEST_TOOL_HAS_ZEROMQ
#include "goby/zeromq/transport/intermodule.h"
#include "goby/zeromq/transport/interprocess.h"
#endif

#include "goby/apps/middleware/test_tool/test_tool_config.pb.h"

#include "fixture.h"
#include "test_file.h"
#include "visualize.h"

using goby::middleware::protobuf::PubSubTestReport;
using goby::middleware::tool::PubSubTestRunner;

namespace goby
{
namespace apps
{
namespace middleware
{
class TestToolConfigurator : public goby::middleware::ProtobufConfigurator<protobuf::TestToolConfig>
{
  public:
    TestToolConfigurator(int argc, char* argv[])
        : goby::middleware::ProtobufConfigurator<protobuf::TestToolConfig>(argc, argv)
    {
        auto& cfg = mutable_cfg();
        if (!cfg.app().glog_config().has_tty_verbosity())
            cfg.mutable_app()->mutable_glog_config()->set_tty_verbosity(
                goby::util::protobuf::GLogConfig::WARN);
    }
};

class TestTool : public goby::middleware::Application<protobuf::TestToolConfig>
{
  public:
    TestTool();

  private:
    void run() override { assert(false); }
};

class TestRunTool : public goby::middleware::Application<protobuf::TestRunConfig>,
                    public goby::middleware::ToolSharedLibraryLoader
{
  public:
    TestRunTool() : goby::middleware::ToolSharedLibraryLoader(app_cfg().load_shared_library()) {}

  private:
    void run() override;
};

class TestValidateTool : public goby::middleware::Application<protobuf::TestValidateConfig>,
                         public goby::middleware::ToolSharedLibraryLoader
{
  public:
    TestValidateTool() : goby::middleware::ToolSharedLibraryLoader(app_cfg().load_shared_library())
    {
    }

  private:
    void run() override;
};

class TestVisualizeTool : public goby::middleware::Application<protobuf::TestVisualizeConfig>
{
  private:
    void run() override;
};

} // namespace middleware
} // namespace apps
} // namespace goby

namespace
{
std::atomic<bool> cancel_requested{false};

void on_cancel_signal(int) { cancel_requested = true; }

goby::apps::middleware::LoadedTestFile
prepare(const std::string& path, const google::protobuf::RepeatedPtrField<std::string>& defines,
        bool check)
{
    using namespace goby::apps::middleware;
    Issues define_issues;
    auto values = parse_defines(defines, define_issues);
    auto loaded = load_test_file(path, values);
    loaded.issues.insert(loaded.issues.begin(), define_issues.begin(), define_issues.end());
    finalize_endpoints(loaded.file);
    if (loaded.parsed)
        expand_test_runs(loaded.file, values, loaded.issues);
    if (check && loaded.parsed)
    {
        load_test_libraries(loaded.file, loaded.issues);
        validate_test_file(loaded.file, loaded.issues);
    }
    return loaded;
}

void write_output(const std::string& out, const std::string& output_file)
{
    if (output_file.empty())
    {
        std::cout << out << std::flush;
    }
    else
    {
        std::ofstream fout(output_file);
        fout << out;
    }
}

std::vector<std::unique_ptr<goby::middleware::tool::PubSubTestEndpoint>>
make_endpoints(const goby::apps::middleware::protobuf::TestFile& file,
               const goby::apps::middleware::protobuf::TestRunConfig& cfg)
{
    using goby::middleware::tool::PubSubTestEndpoint;
    using goby::middleware::tool::PubSubTestPortalEndpoint;
    std::vector<std::unique_ptr<PubSubTestEndpoint>> endpoints;
    for (const auto& ep : file.endpoint())
    {
        const bool interprocess = ep.layer() == goby::middleware::protobuf::LAYER_INTERPROCESS;
        const std::string client = cfg.app().name() + "." + ep.name();
        if (ep.has_zeromq())
        {
#ifdef GOBY_TEST_TOOL_HAS_ZEROMQ
            using Config = goby::zeromq::protobuf::InterProcessPortalConfig;
            Config merged = cfg.zeromq();
            merged.MergeFrom(ep.zeromq());
            if (!merged.has_client_name())
                merged.set_client_name(client);
            if (interprocess)
                endpoints.push_back(
                    std::make_unique<
                        PubSubTestPortalEndpoint<goby::zeromq::InterProcessPortal<>, Config>>(
                        ep.name(), merged, "goby test"));
            else
                endpoints.push_back(
                    std::make_unique<
                        PubSubTestPortalEndpoint<goby::zeromq::InterModulePortal<>, Config>>(
                        ep.name(), merged, "goby test"));
#endif
        }
        else
        {
            using Config = goby::udpm::protobuf::InterProcessPortalConfig;
            Config merged = cfg.udpm();
            merged.MergeFrom(ep.udpm());
            if (!merged.has_client_name())
                merged.set_client_name(client);
            if (interprocess)
                endpoints.push_back(
                    std::make_unique<
                        PubSubTestPortalEndpoint<goby::udpm::InterProcessPortal<>, Config>>(
                        ep.name(), merged, "goby test"));
            else
                endpoints.push_back(
                    std::make_unique<
                        PubSubTestPortalEndpoint<goby::udpm::InterModulePortal<>, Config>>(
                        ep.name(), merged, "goby test"));
        }
    }
    return endpoints;
}
} // namespace

int main(int argc, char* argv[])
{
    return goby::run<goby::apps::middleware::TestTool>(
        goby::apps::middleware::TestToolConfigurator(argc, argv));
}

goby::apps::middleware::TestTool::TestTool()
{
    using Config = goby::apps::middleware::protobuf::TestToolConfig;
    goby::middleware::ToolHelper tool_helper(app_cfg().app().binary(), app_cfg().app().tool_cfg(),
                                             Config::Action_descriptor());

    int exit_code = 0;
    if (!tool_helper.perform_action(app_cfg().action()))
    {
        switch (app_cfg().action())
        {
            case Config::help:
                int action_for_help;
                if (!tool_helper.help(&action_for_help))
                {
                    switch (action_for_help)
                    {
                        case Config::run: tool_helper.help<TestRunTool>(action_for_help); break;
                        case Config::validate:
                            tool_helper.help<TestValidateTool>(action_for_help);
                            break;
                        case Config::visualize:
                            tool_helper.help<TestVisualizeTool>(action_for_help);
                            break;
                        default:
                            throw(goby::Exception(
                                "Help was expected to be handled by external tool"));
                    }
                }
                break;
            case Config::run: exit_code = tool_helper.run_subtool<TestRunTool>(); break;
            case Config::validate: exit_code = tool_helper.run_subtool<TestValidateTool>(); break;
            case Config::visualize: exit_code = tool_helper.run_subtool<TestVisualizeTool>(); break;
            default: throw(goby::Exception("Action was expected to be handled by external tool"));
        }
    }
    quit(exit_code);
}

void goby::apps::middleware::TestRunTool::run()
{
    const auto& cfg = app_cfg();

    struct sigaction sa = {};
    sa.sa_handler = on_cancel_signal;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);
    enable_child_subreaper();
    auto cancelled = []() { return cancel_requested.load(); };

    PubSubTestReport report;
    report.set_result(goby::middleware::protobuf::ERROR);

    auto loaded = prepare(cfg.test_file(), cfg.define(), true);
    report.set_name(loaded.file.test().has_name() ? loaded.file.test().name() : cfg.test_file());

    if (cfg.repeat() < 1)
    {
        report.set_error("--repeat must be at least 1");
    }
    else if (!loaded.issues.empty())
    {
        report.set_error("the test file is invalid");
        for (const auto& issue : loaded.issues) *report.add_issue() = issue;
    }
    else
    {
        try
        {
            FixtureSet fixtures(loaded.file.fixture(), cfg.log_dir());
            if (!fixtures.start_all(cancelled))
            {
                report.set_error(cancelled() ? "interrupted" : "a fixture did not become ready");
            }
            else if (cfg.repeat() == 1)
            {
                PubSubTestRunner runner(loaded.file.test(), make_endpoints(loaded.file, cfg));
                runner.set_cancel_check(cancelled);
                report = runner.run();
                if (report.name().empty())
                    report.set_name(cfg.test_file());
            }
            else
            {
                report.set_result(goby::middleware::protobuf::PASS);
                double elapsed = 0;
                for (unsigned k = 1; k <= cfg.repeat() && !cancelled(); ++k)
                {
                    // fresh endpoints per pass so no subscription carries over
                    PubSubTestRunner runner(loaded.file.test(), make_endpoints(loaded.file, cfg));
                    runner.set_cancel_check(cancelled);
                    auto& pass = *report.add_pass();
                    pass = runner.run();
                    pass.set_name("pass " + std::to_string(k) + "/" + std::to_string(cfg.repeat()));
                    elapsed += pass.elapsed();
                    report.set_result(std::max(report.result(), pass.result()));
                }
                if (cancelled())
                {
                    report.set_result(goby::middleware::protobuf::ERROR);
                    report.set_error("interrupted");
                }
                report.set_elapsed_with_units(elapsed * boost::units::si::seconds);
            }
            fixtures.stop_all();
            fixtures.fill_reports(*report.mutable_fixture(),
                                  report.result() != goby::middleware::protobuf::PASS);
        }
        catch (const std::exception& e)
        {
            report.set_result(goby::middleware::protobuf::ERROR);
            report.set_error(e.what());
        }
    }

    write_output(cfg.output_format() == protobuf::TestRunConfig::JSON
                     ? PubSubTestRunner::render_json(report)
                     : PubSubTestRunner::render_human(report),
                 cfg.output_file());

    switch (report.result())
    {
        case goby::middleware::protobuf::PASS: quit(0); break;
        case goby::middleware::protobuf::FAIL: quit(1); break;
        default: quit(2); break;
    }
}

void goby::apps::middleware::TestValidateTool::run()
{
    const auto& cfg = app_cfg();
    auto loaded = prepare(cfg.test_file(), cfg.define(), true);

    goby::middleware::protobuf::PubSubTestValidation validation;
    validation.set_file(cfg.test_file());
    validation.set_valid(loaded.issues.empty());
    for (const auto& issue : loaded.issues) *validation.add_issue() = issue;

    if (cfg.output_format() == protobuf::TestRunConfig::JSON)
    {
        std::cout << PubSubTestRunner::render_json(validation) << std::flush;
    }
    else if (validation.valid())
    {
        const auto& f = loaded.file;
        std::cout << cfg.test_file() << ": valid (" << f.endpoint_size() << " endpoint(s), "
                  << f.fixture_size() << " fixture(s), " << f.test().run_size() << " run(s))"
                  << std::endl;
    }
    else
    {
        std::cout << cfg.test_file() << ": " << validation.issue_size() << " problem(s)\n";
        for (const auto& issue : validation.issue())
            std::cout << "  " << issue.location() << ": " << issue.message() << "\n";
        std::cout << std::flush;
    }
    quit(validation.valid() ? 0 : 1);
}

void goby::apps::middleware::TestVisualizeTool::run()
{
    const auto& cfg = app_cfg();
    auto loaded = prepare(cfg.test_file(), cfg.define(), false);
    for (const auto& issue : loaded.issues)
        std::cerr << "warning: " << issue.location() << ": " << issue.message() << std::endl;
    if (!loaded.parsed)
    {
        quit(1);
        return;
    }

    const std::string title =
        loaded.file.test().has_name() ? loaded.file.test().name() : cfg.test_file();
    write_output(test_file_to_dot(loaded.file, title), cfg.output_file());
    quit(0);
}
