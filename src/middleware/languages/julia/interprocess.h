#include <jlcxx/functions.hpp>
#include <jlcxx/jlcxx.hpp>

#include <google/protobuf/text_format.hpp>

#include "goby/util/debug_logger.h"
#include "goby/zeromq/transport/interprocess.h"

template <typename Config> void init(std::string config)
{
    Config cfg;
    google::protobuf::TextFormat::Parser parser;
    goby::util::FlexOStreamErrorCollector error_collector(config);
    parser.RecordErrorsTo(&error_collector);
    parser.AllowPartialMessage(false);
    parser.ParseFromString(config, &cfg);

    goby::glog.add_stream(goby::util::logger::DEBUG2, &std::cerr);
    goby::glog.set_name("julia_app");
    goby::glog.set_lock_action(goby::util::logger_lock::lock);
    goby::zeromq::protobuf::InterProcessPortalConfig cfg;
    interprocess.reset(new goby::zeromq::InterProcessPortal<>(cfg));
}

void poll() { interprocess->poll(std::chrono::seconds(0)); }

void publish(goby::middleware::protobuf::LatLonPoint pb)
{
    goby::glog.is_verbose() && goby::glog << "Goby: Published: " << pb.ShortDebugString()
                                          << std::endl;
    interprocess->publish<grp>(pb);
}

void subscribe(std::string function_name, std::string module_name)
{
    jlcxx::JuliaFunction cb(function_name, module_name);
    interprocess->subscribe<grp2>([=](const goby::middleware::protobuf::LatLonPoint& pb)
                                  { cb(pb); });
}
