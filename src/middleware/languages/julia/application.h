// Copyright 2025-2026:
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

#include <jlcxx/functions.hpp>
#include <jlcxx/jlcxx.hpp>

#include "goby/middleware/application/interface.h"
#include "goby/middleware/group.h"
#include "goby/middleware/marshalling/interface.h"
#include "goby/time.h"

namespace goby
{
namespace middleware
{
namespace julia
{

enum class PubSubLayer
{
    INTERTHREAD = 0,
    INTERPROCESS = 1,
    INTERMODULE = 2
};

template <typename AppBase> class Application : public AppBase
{
  public:
    Application() { loop_ = std::make_unique<jlcxx::JuliaFunction>("cxx_loop", "Goby"); }
    void loop() override { (*loop_)(); }

  private:
    std::unique_ptr<jlcxx::JuliaFunction> loop_;
};

struct Identifier
{
    goby::middleware::julia::PubSubLayer layer;
    std::string type_name;
    int scheme;
    std::string group;
};

bool operator==(const Identifier& i1, const Identifier& i2)
{
    return i1.layer == i2.layer && i1.type_name == i2.type_name && i1.scheme == i2.scheme &&
           i1.group == i2.group;
}

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

    void run() { app_ptr_->__run(); }

    ApplicationWrapper& interprocess() { return *this; }

    void publish(PubSubLayer layer, std::string type_name, int scheme, std::string group,
                 const std::vector<std::uint8_t>& bytes)
    {
        app_ptr_->publish(Identifier(layer, type_name, scheme, group), bytes);
    }

    void subscribe(PubSubLayer layer, std::string type_name, int scheme, std::string group,
                   std::string func, std::string module)
    {
        app_ptr_->subscribe(Identifier(layer, type_name, scheme, group), func, module);
    }

    void set_loop_frequency_hertz(double freq) { app_ptr_->set_loop_frequency_hertz(freq); }

  private:
    std::unique_ptr<App> app_ptr_;
    std::map<std::string, goby::middleware::DynamicGroup> subscription_groups_;
};

template <typename App>
inline void define_julia_module(jlcxx::Module& types, const std::string& app_name)
{
    types.add_bits<PubSubLayer>("PubSubLayer", jlcxx::julia_type("CppEnum"));
    types.set_const("INTERTHREAD", PubSubLayer::INTERTHREAD);
    types.set_const("INTERPROCESS", PubSubLayer::INTERPROCESS);
    types.set_const("INTERMODULE", PubSubLayer::INTERMODULE);

    types.add_bits<MarshallingScheme::MarshallingSchemeEnum>("MarshallingScheme",
                                                             jlcxx::julia_type("CppEnum"));
    types.set_const("NULL_SCHEME", MarshallingScheme::NULL_SCHEME);
    types.set_const("PROTOBUF", MarshallingScheme::PROTOBUF);
    types.set_const("JSON", MarshallingScheme::JSON);

    types.template add_type<ApplicationWrapper<App>>(app_name)
        .template constructor<std::string>()
        .method("cxx_run", &ApplicationWrapper<App>::run)
        .method("cxx_publish", &ApplicationWrapper<App>::publish)
        .method("cxx_subscribe", &ApplicationWrapper<App>::subscribe)
        .method("cxx_set_loop_frequency_hertz", &ApplicationWrapper<App>::set_loop_frequency_hertz);
}

template <typename DataType, int scheme>
std::vector<std::uint8_t> serialize_uint8(const DataType& msg)
{
    std::vector<char> out =
        goby::middleware::SerializerParserHelper<DataType, scheme>::serialize(msg);
    return std::vector<std::uint8_t>(out.begin(), out.end());
}

} // namespace julia
} // namespace middleware
} // namespace goby

std::ostream& operator<<(std::ostream& os, const goby::middleware::julia::Identifier& i)
{
    std::string layer;

    switch (i.layer)
    {
        case goby::middleware::julia::PubSubLayer::INTERTHREAD: layer = "interthread"; break;
        case goby::middleware::julia::PubSubLayer::INTERPROCESS: layer = "interprocess"; break;
        case goby::middleware::julia::PubSubLayer::INTERMODULE: layer = "intermodule"; break;
    }

    return (os << "layer: " << layer << ", type_name: \"" << i.type_name
               << "\", scheme: " << goby::middleware::MarshallingScheme::to_string(i.scheme)
               << ", group: \"" << i.group << "\"");
}

// Macros for use by the autogenerated code to define the C++ side of the pub/sub setup
#define GOBY_JULIA_IF_PUBLICATION(SCHEME, LAYER_ENUM, LAYER_FUNCTION, GROUP, TYPE)                \
    if (id == goby::middleware::julia::Identifier{                                                \
                  goby::middleware::julia::PubSubLayer::LAYER_ENUM, TYPE::descriptor()->name(),   \
                  goby::middleware::MarshallingScheme::SCHEME, GROUP})                            \
    {                                                                                             \
        decltype(bytes.end()) actual_end;                                                         \
        auto msg = goby::middleware::SerializerParserHelper<                                      \
            TYPE, goby::middleware::MarshallingScheme::SCHEME>::parse(bytes.begin(), bytes.end(), \
                                                                      actual_end);                \
        LAYER_FUNCTION().publish<GROUP>(msg);                                                     \
        return;                                                                                   \
    }

#define GOBY_JULIA_IF_SUBSCRIPTION(SCHEME, LAYER_ENUM, LAYER_FUNCTION, GROUP, TYPE)             \
    if (id == goby::middleware::julia::Identifier(                                              \
                  goby::middleware::julia::PubSubLayer::LAYER_ENUM, TYPE::descriptor()->name(), \
                  goby::middleware::MarshallingScheme::SCHEME, GROUP))                          \
    {                                                                                           \
        LAYER_FUNCTION().subscribe<GROUP>(                                                      \
            [=](const TYPE& pb)                                                                 \
            {                                                                                   \
                std::vector<std::uint8_t> bytes = goby::middleware::julia::serialize_uint8<     \
                    TYPE, goby::middleware::MarshallingScheme::SCHEME>(pb);                     \
                jlcxx::JuliaFunction cb(func, module);                                          \
                cb(id.layer, id.type_name, id.scheme, id.group, bytes);                         \
            });                                                                                 \
        return;                                                                                 \
    }

#define GOBY_JULIA_FAIL(PUBLISH_OR_SUBSCRIBE)                                                \
    goby::glog.is_die() &&                                                                   \
        goby::glog << PUBLISH_OR_SUBSCRIBE " not defined for these parameters: [" << id      \
                   << "]. Please include in interfaces.yml and re-generate to include them." \
                   << std::endl;

// used to stringify application name
#define GOBY_JULIA_QUOTE(name) #name
#define GOBY_JULIA_DEFINE_MODULE(APPLICATION_NAME)                      \
    JLCXX_MODULE define_julia_module(jlcxx::Module& types)              \
    {                                                                   \
        goby::middleware::julia::define_julia_module<APPLICATION_NAME>( \
            types, GOBY_JULIA_QUOTE(APPLICATION_NAME));                 \
    }
