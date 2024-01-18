#include "goby/middleware/marshalling/dccl.h"
#include "goby/middleware/marshalling/json.h"
#include "goby/middleware/marshalling/protobuf.h"

#include "goby/middleware/log/dccl_log_plugin.h"
#include "goby/middleware/log/json_log_plugin.h"
#include "goby/middleware/log/protobuf_log_plugin.h"

#include "goby/middleware/application/configuration_reader.h"
#include "goby/middleware/application/interface.h"
#include "goby/middleware/application/tool.h"
#include "goby/zeromq/application/single_thread.h"
#include "goby/zeromq/protobuf/tool_config.pb.h"

using goby::glog;

namespace goby
{
namespace apps
{
namespace zeromq
{
class ZeroMQToolConfigurator
    : public goby::middleware::ProtobufConfigurator<protobuf::ZeroMQToolConfig>
{
  public:
    ZeroMQToolConfigurator(int argc, char* argv[])
        : goby::middleware::ProtobufConfigurator<protobuf::ZeroMQToolConfig>(argc, argv)
    {
        auto& cfg = mutable_cfg();
        if (!cfg.app().glog_config().has_tty_verbosity())
            cfg.mutable_app()->mutable_glog_config()->set_tty_verbosity(
                goby::util::protobuf::GLogConfig::WARN);
    }
};

class ZeroMQTool : public goby::middleware::Application<protobuf::ZeroMQToolConfig>
{
  public:
    ZeroMQTool();
    ~ZeroMQTool() override {}

  private:
    // never gets called
    void run() override { assert(false); }

  private:
};

class PublishTool : public goby::zeromq::SingleThreadApplication<protobuf::PublishToolConfig>
{
  public:
    PublishTool();
    ~PublishTool() override
    {
        for (void* handle : dl_handles_) dlclose(handle);
    }
    void loop() override;

  private:
    std::vector<void*> dl_handles_;
};

class SubscribeTool : public goby::zeromq::SingleThreadApplication<protobuf::SubscribeToolConfig>
{
  public:
    SubscribeTool();
    ~SubscribeTool() override
    {
        for (void* handle : dl_handles_) dlclose(handle);
    }

  private:
    std::map<int, std::unique_ptr<goby::middleware::log::LogPlugin>> plugins_;
    std::vector<void*> dl_handles_;
};

} // namespace zeromq
} // namespace apps
} // namespace goby

int main(int argc, char* argv[])
{
    return goby::run<goby::apps::zeromq::ZeroMQTool>(
        goby::apps::zeromq::ZeroMQToolConfigurator(argc, argv));
}

goby::apps::zeromq::ZeroMQTool::ZeroMQTool()
{
    goby::middleware::ToolHelper tool_helper(
        app_cfg().app().binary(), app_cfg().app().tool_cfg(),
        goby::apps::zeromq::protobuf::ZeroMQToolConfig::Action_descriptor());

    if (!tool_helper.perform_action(app_cfg().action()))
    {
        switch (app_cfg().action())
        {
            case goby::apps::zeromq::protobuf::ZeroMQToolConfig::help:
                int action_for_help;
                if (!tool_helper.help(&action_for_help))
                {
                    switch (action_for_help)
                    {
                        case goby::apps::zeromq::protobuf::ZeroMQToolConfig::publish:
                            tool_helper.help<goby::apps::zeromq::PublishTool>(action_for_help);
                            break;

                        case goby::apps::zeromq::protobuf::ZeroMQToolConfig::subscribe:
                            tool_helper.help<goby::apps::zeromq::SubscribeTool>(action_for_help);
                            break;

                        default:
                            throw(goby::Exception(
                                "Help was expected to be handled by external tool"));
                            break;
                    }
                }
                break;

            case goby::apps::zeromq::protobuf::ZeroMQToolConfig::publish:
                tool_helper.run_subtool<goby::apps::zeromq::PublishTool>();
                break;

            case goby::apps::zeromq::protobuf::ZeroMQToolConfig::subscribe:
                tool_helper.run_subtool<goby::apps::zeromq::SubscribeTool>();
                break;

            default:
                // perform action will call 'exec' if an external tool performs the action,
                // so if we are continuing, this didn't happen
                throw(goby::Exception("Action was expected to be handled by external tool"));
                break;
        }
    }

    quit(0);
}

goby::apps::zeromq::PublishTool::PublishTool()
    : goby::zeromq::SingleThreadApplication<protobuf::PublishToolConfig>(1.0 *
                                                                         boost::units::si::hertz)
{
    for (const auto& lib : cfg().load_shared_library())
    {
        void* lib_handle = dlopen(lib.c_str(), RTLD_LAZY);
        if (!lib_handle)
            glog.is_die() && glog << "Failed to open library: " << lib << std::endl;
        dl_handles_.push_back(lib_handle);
    }

    int scheme = goby::middleware::MarshallingScheme::from_string(cfg().scheme());
    std::string scheme_str = goby::middleware::MarshallingScheme::to_string(scheme);
    goby::middleware::DynamicGroup group(cfg().group());
    switch (scheme)
    {
        case goby::middleware::MarshallingScheme::DCCL:
        case goby::middleware::MarshallingScheme::PROTOBUF:
        {
            if (!cfg().has_type())
                glog.is_die() && glog << "You must specify a type for scheme: PROTOBUF. See --help "
                                         "for more information"
                                      << std::endl;

            // use TextFormat
            auto pb_msg = dccl::DynamicProtobufManager::new_protobuf_message<
                std::shared_ptr<google::protobuf::Message>>(cfg().type());
            google::protobuf::TextFormat::Parser parser;
            goby::util::FlexOStreamErrorCollector error_collector(cfg().value());
            parser.RecordErrorsTo(&error_collector);
            parser.AllowPartialMessage(false);
            parser.ParseFromString(cfg().value(), pb_msg.get());

            if (scheme == goby::middleware::MarshallingScheme::DCCL)
                interprocess()
                    .publish_dynamic<google::protobuf::Message,
                                     goby::middleware::MarshallingScheme::DCCL>(pb_msg, group);
            else if (scheme == goby::middleware::MarshallingScheme::PROTOBUF)
                interprocess()
                    .publish_dynamic<google::protobuf::Message,
                                     goby::middleware::MarshallingScheme::PROTOBUF>(pb_msg, group);
            break;
        }

        case goby::middleware::MarshallingScheme::JSON:
        {
            nlohmann::json j(cfg().value());
            interprocess().publish_dynamic<nlohmann::json>(j, group);
            break;
        }

        default:
            glog.is_die() && glog << "Scheme " << scheme_str
                                  << " is not implemented for 'goby zeromq publish'" << std::endl;
    }
}

void goby::apps::zeromq::PublishTool::loop()
{
    static int i = 0;
    ++i;
    if (i > 1) // exit on second call of loop, plenty of time for publish to go through
        quit(0);
}

goby::apps::zeromq::SubscribeTool::SubscribeTool()
{
    std::set<int> schemes{goby::middleware::MarshallingScheme::ALL_SCHEMES};

    for (const auto& lib : cfg().load_shared_library())
    {
        void* lib_handle = dlopen(lib.c_str(), RTLD_LAZY);
        if (!lib_handle)
            glog.is_die() && glog << "Failed to open library: " << lib << std::endl;
        dl_handles_.push_back(lib_handle);
    }

    if (cfg().has_scheme())
    {
        int scheme = goby::middleware::MarshallingScheme::from_string(cfg().scheme());
        schemes = {scheme};
    }

    plugins_[goby::middleware::MarshallingScheme::PROTOBUF] =
        std::make_unique<goby::middleware::log::ProtobufPlugin>();
    plugins_[goby::middleware::MarshallingScheme::DCCL] =
        std::make_unique<goby::middleware::log::DCCLPlugin>();
    plugins_[goby::middleware::MarshallingScheme::JSON] =
        std::make_unique<goby::middleware::log::JSONPlugin>();

    interprocess().subscribe_regex(
        [this](const std::vector<unsigned char>& bytes, int scheme, const std::string& type,
               const goby::middleware::Group& group)
        {
            std::regex exclude_pattern("goby::zeromq::_internal.*");
            if (std::regex_match(std::string(group), exclude_pattern) &&
                !cfg().include_internal_groups())
                return;

            goby::middleware::log::LogEntry log_entry(bytes, scheme, type, group);
            std::string debug_text;

            auto plugin = plugins_.find(log_entry.scheme());
            if (plugin == plugins_.end())
            {
                debug_text = std::string("Message of " + std::to_string(bytes.size()) + " bytes");
            }
            else
            {
                try
                {
                    debug_text = plugin->second->debug_text_message(log_entry);
                }
                catch (goby::middleware::log::LogException& e)
                {
                    debug_text = "Unable to parse message of " +
                                 std::to_string(log_entry.data().size()) +
                                 " bytes. Reason: " + e.what();
                }
            }

            // use similar format to goby_log_tool DEBUG_TEXT
            std::cout << scheme << " | " << group << " | " << type << " | "
                      << goby::time::convert<boost::posix_time::ptime>(log_entry.timestamp())
                      << " | " << debug_text << std::endl;
        },
        schemes, cfg().type_regex(), cfg().group_regex());
}
