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

#include <fstream>
#include <iostream>

#include "goby/middleware/tool/config_file.h"
#include "goby/middleware/tool/pubsub_test.h"
#include "goby/udpm/transport/intermodule.h"
#include "goby/udpm/transport/interprocess.h"

#ifdef GOBY_TEST_TOOL_HAS_ZEROMQ
#include "goby/zeromq/transport/intermodule.h"
#include "goby/zeromq/transport/interprocess.h"
#endif

#include "test_tool.h"

using goby::apps::middleware::protobuf::TestToolConfig;

namespace
{
constexpr bool has_zeromq()
{
#ifdef GOBY_TEST_TOOL_HAS_ZEROMQ
    return true;
#else
    return false;
#endif
}
} // namespace

goby::apps::middleware::TestToolConfigurator::TestToolConfigurator(int argc, char* argv[])
    : goby::middleware::ProtobufConfigurator<TestToolConfig>(argc, argv)
{
    auto& cfg = mutable_cfg();
    try
    {
        if (cfg.has_test_file())
            goby::middleware::tool::load_config_file(cfg.test_file(), cfg);
    }
    catch (const goby::Exception& e)
    {
        error_ = e.what();
        return;
    }
    if (!cfg.has_test())
    {
        error_ = "no test defined: provide a test file or a 'test' block";
        return;
    }

    if (cfg.endpoint_size() == 0)
    {
        auto& ep = *cfg.add_endpoint();
        ep.set_name("default");
        ep.set_layer(goby::middleware::protobuf::LAYER_INTERPROCESS);
    }

    for (auto& ep : *cfg.mutable_endpoint())
    {
        if (ep.transport_case() == TestToolConfig::Endpoint::TRANSPORT_NOT_SET)
        {
            if (has_zeromq())
                ep.mutable_zeromq();
            else
                ep.mutable_udpm();
        }

        // endpoint settings layer over the tool-wide defaults for that transport
        if (ep.has_zeromq())
        {
            if (!has_zeromq())
            {
                error_ = "endpoint '" + ep.name() +
                         "' uses zeromq, but this build of goby has no zeromq support";
                return;
            }
            auto merged = cfg.zeromq();
            merged.MergeFrom(ep.zeromq());
            if (!merged.has_client_name())
                merged.set_client_name(cfg.app().name() + "." + ep.name());
            *ep.mutable_zeromq() = merged;
        }
        else
        {
            auto merged = cfg.udpm();
            merged.MergeFrom(ep.udpm());
            if (!merged.has_client_name())
                merged.set_client_name(cfg.app().name() + "." + ep.name());
            *ep.mutable_udpm() = merged;
        }
    }
}

// errors are reported from validate() so that goby::run prints them and exits cleanly
void goby::apps::middleware::TestToolConfigurator::validate() const
{
    ProtobufConfigurator::validate();
    if (!error_.empty())
        throw(goby::middleware::ConfigException(error_));
}

void goby::apps::middleware::TestTool::run()
{
    using goby::middleware::tool::PubSubTestEndpoint;
    using goby::middleware::tool::PubSubTestPortalEndpoint;
    using goby::middleware::tool::PubSubTestRunner;

    const auto& cfg = app_cfg();
    int exit_code = 2;
    try
    {
        std::vector<std::unique_ptr<PubSubTestEndpoint>> endpoints;
        for (const auto& ep : cfg.endpoint())
        {
            const bool interprocess = ep.layer() == goby::middleware::protobuf::LAYER_INTERPROCESS;
            if (!interprocess && ep.layer() != goby::middleware::protobuf::LAYER_INTERMODULE)
                throw goby::Exception("endpoint '" + ep.name() + "': unsupported layer " +
                                      goby::middleware::protobuf::Layer_Name(ep.layer()));

            if (ep.has_zeromq())
            {
#ifdef GOBY_TEST_TOOL_HAS_ZEROMQ
                using Config = goby::zeromq::protobuf::InterProcessPortalConfig;
                if (interprocess)
                    endpoints.push_back(
                        std::make_unique<
                            PubSubTestPortalEndpoint<goby::zeromq::InterProcessPortal<>, Config>>(
                            ep.name(), ep.zeromq(), "goby test"));
                else
                    endpoints.push_back(
                        std::make_unique<
                            PubSubTestPortalEndpoint<goby::zeromq::InterModulePortal<>, Config>>(
                            ep.name(), ep.zeromq(), "goby test"));
#endif
            }
            else
            {
                using Config = goby::udpm::protobuf::InterProcessPortalConfig;
                if (interprocess)
                    endpoints.push_back(
                        std::make_unique<
                            PubSubTestPortalEndpoint<goby::udpm::InterProcessPortal<>, Config>>(
                            ep.name(), ep.udpm(), "goby test"));
                else
                    endpoints.push_back(
                        std::make_unique<
                            PubSubTestPortalEndpoint<goby::udpm::InterModulePortal<>, Config>>(
                            ep.name(), ep.udpm(), "goby test"));
            }
        }

        PubSubTestRunner runner(cfg.test(), std::move(endpoints));
        auto report = runner.run();

        const std::string out = cfg.output_format() == TestToolConfig::JSON
                                    ? PubSubTestRunner::render_json(report)
                                    : PubSubTestRunner::render_human(report);
        if (cfg.has_output_file())
        {
            std::ofstream fout(cfg.output_file());
            fout << out;
        }
        else
        {
            std::cout << out << std::flush;
        }

        switch (report.result())
        {
            case goby::middleware::protobuf::PASS: exit_code = 0; break;
            case goby::middleware::protobuf::FAIL: exit_code = 1; break;
            default: exit_code = 2; break;
        }
    }
    catch (const std::exception& e)
    {
        std::cerr << "goby test: " << e.what() << std::endl;
    }
    quit(exit_code);
}
