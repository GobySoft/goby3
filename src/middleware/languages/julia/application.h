#include <jlcxx/functions.hpp>
#include <jlcxx/jlcxx.hpp>

#include "goby/middleware/application/interface.h"
#include "goby/time.h"

namespace goby
{
namespace middleware
{
namespace julia
{

enum class PubSubLayer
{
    INTERTHREAD,
    INTERPROCESS,
    INTERMODULE
};

template <typename App> class ApplicationWrapper
{
  public:
    ApplicationWrapper(std::string config) : ApplicationWrapper(config, std::string()) {}
    ApplicationWrapper(std::string config, std::string loop_function_name)
    {
        typename App::ConfigType cfg;
        google::protobuf::TextFormat::Parser parser;
        goby::util::FlexOStreamErrorCollector error_collector(config);
        parser.RecordErrorsTo(&error_collector);
        parser.AllowPartialMessage(false);
        parser.ParseFromString(config, &cfg);

        App::app_cfg_.reset(new typename App::ConfigType(cfg));
        App::app3_base_configuration_.reset(new goby::middleware::protobuf::AppConfig(cfg.app()));

        // TODO - don't copy from middleware/application/interface.h if possible
        if (App::app3_base_configuration_->simulation().time().use_sim_time())
        {
            goby::time::SimulatorSettings::using_sim_time = true;
            goby::time::SimulatorSettings::warp_factor =
                App::app3_base_configuration_->simulation().time().warp_factor();
            if (App::app3_base_configuration_->simulation().time().has_reference_microtime())
                goby::time::SimulatorSettings::reference_time =
                    std::chrono::system_clock::time_point(std::chrono::microseconds(
                        App::app3_base_configuration_->simulation().time().reference_microtime()));
        }

        app_ptr_.reset(new App);
        app_ptr_->set_loop_function_name(loop_function_name);
    }

    void run() { app_ptr_->__run(); }

    ApplicationWrapper& interprocess() { return *this; }

    void publish(PubSubLayer layer, std::string type_name, int scheme, std::string group,
                 const std::vector<char>& bytes)
    {
        app_ptr_->publish(std::make_tuple(layer, type_name, scheme, group), bytes);
    }

    void subscribe(PubSubLayer layer, std::string type_name, int scheme, std::string group,
                   std::string func, std::string module)
    {
        app_ptr_->subscribe(std::make_tuple(layer, type_name, scheme, group), func, module);
    }

  private:
    std::unique_ptr<App> app_ptr_;
    std::map<std::string, goby::middleware::DynamicGroup> subscription_groups_;
};

} // namespace julia
} // namespace middleware
} // namespace goby
