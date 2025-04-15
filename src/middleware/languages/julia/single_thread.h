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

template <typename App> class ApplicationWrapper
{
  public:
    ApplicationWrapper(std::string config)
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
    }

    void run_one()
    {
        app_ptr_->run_one();
        app_ptr_->check_rotate_glog_file();
    }

    void publish(goby::middleware::protobuf::LatLonPoint pb) { app_ptr_->publish(pb); }
    void subscribe(std::string function_name) { app_ptr_->subscribe(function_name); }

  private:
    std::unique_ptr<App> app_ptr_;
};

} // namespace julia
} // namespace middleware
} // namespace goby
