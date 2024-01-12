// Copyright 2018-2021:
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

#ifndef GOBY_MIDDLEWARE_APPLICATION_CONFIGURATOR_H
#define GOBY_MIDDLEWARE_APPLICATION_CONFIGURATOR_H

#include "goby/middleware/application/configuration_reader.h"
#include "goby/middleware/protobuf/app_config.pb.h"

namespace goby
{
namespace middleware
{
/// \brief Defines the interface to a "configurator", a class that can read command line parameters (argc, argv) and produce a configuration object.
///
/// Configurators are used to read command line parameters (and subsequently possibly open one or more configuration files) to populate the values in a configuration object that is used by the code to be configured (SingleThreadApplication, MultiThreadApplication, SimpleThread, etc.).
/// \tparam Config The type of the configuration object produced by the configurator
template <typename Config> class ConfiguratorInterface
{
  public:
    /// \brief The configuration object produced from the command line parameters
    const Config& cfg() const { return cfg_; }

    /// \brief Subset of the configuration used to configure the Application itself
    /// \todo Change AppConfig to a C++ struct (not a Protobuf message)
    virtual const protobuf::AppConfig& app_configuration() const { return app_configuration_; }

    /// \brief Override to validate the configuration
    ///
    /// \throw ConfigException if the configuration is not valid
    virtual void validate() const {}

    /// \brief Override to customize how ConfigException errors are handled.
    virtual void handle_config_error(middleware::ConfigException& e) const
    {
        std::cerr << "Invalid configuration: " << e.what() << std::endl;
    }

    /// \brief Override to output the configuration object as a string
    virtual std::string str() const = 0;

  protected:
    /// \brief Derived classes can modify the configuration as needed in their constructor
    Config& mutable_cfg() { return cfg_; }

    /// \brief Derived classes can modify the application configuration as needed in their constructor
    virtual protobuf::AppConfig& mutable_app_configuration() { return app_configuration_; }

  private:
    Config cfg_;
    protobuf::AppConfig app_configuration_;
};

/// \brief Implementation of ConfiguratorInterface for Google Protocol buffers
///
/// \tparam Config The Protobuf message that represents the parsed configuration
template <typename Config> class ProtobufConfigurator : public ConfiguratorInterface<Config>
{
  public:
    /// \brief Constructs a ProtobufConfigurator. Typically passed as a parameter to goby::run
    ///
    /// \param argc Command line argument count
    /// \param argv Command line parameters
    ProtobufConfigurator(int argc, char* argv[]);

    const protobuf::AppConfig& app_configuration() const override { return this->cfg().app(); }

  protected:
    virtual void validate() const override
    {
        middleware::ConfigReader::check_required_cfg(this->cfg(), this->cfg().app().binary());
    }

  private:
    virtual std::string str() const override { return this->cfg().DebugString(); }

    void handle_config_error(middleware::ConfigException& e) const override
    {
        std::cerr << "Invalid configuration: use --help and/or --example_config for more help: "
                  << e.what() << std::endl;
    }

    protobuf::AppConfig& mutable_app_configuration() override
    {
        return *this->mutable_cfg().mutable_app();
    }

    void merge_app_base_cfg(protobuf::AppConfig* base_cfg,
                            const boost::program_options::variables_map& var_map);
};

template <typename Config>
ProtobufConfigurator<Config>::ProtobufConfigurator(int argc, char* argv[])
{
    //
    // read the configuration
    //
    Config& cfg = this->mutable_cfg();

    boost::program_options::variables_map var_map;
    try
    {
        std::string application_name;
        std::string binary_name;

        // we will check it later in validate()
        bool check_required_cfg = false;
        boost::program_options::options_description od{"All options"};
        middleware::ConfigReader::read_cfg(argc, argv, &cfg, &application_name, &binary_name, &od,
                                           &var_map, check_required_cfg);

        cfg.mutable_app()->set_name(application_name);
        cfg.mutable_app()->set_binary(binary_name);
        // incorporate some parts of the AppBaseConfig that are middleware
        // with gobyd (e.g. Verbosity)
        merge_app_base_cfg(cfg.mutable_app(), var_map);
    }
    catch (middleware::ConfigException& e)
    {
        handle_config_error(e);
        throw;
    }

    // TODO: convert to C++ struct app3 configuration format
    this->mutable_app_configuration() = *cfg.mutable_app();
}

template <typename Config>
void ProtobufConfigurator<Config>::merge_app_base_cfg(
    goby::middleware::protobuf::AppConfig* base_cfg,
    const boost::program_options::variables_map& var_map)
{
    if (var_map.count("ncurses"))
    {
        base_cfg->mutable_glog_config()->set_show_gui(true);
    }

    if (var_map.count("verbose"))
    {
        switch (var_map["verbose"].as<std::string>().size())
        {
            default:
            case 0:
                base_cfg->mutable_glog_config()->set_tty_verbosity(
                    util::protobuf::GLogConfig::VERBOSE);
                break;
            case 1:
                base_cfg->mutable_glog_config()->set_tty_verbosity(
                    util::protobuf::GLogConfig::DEBUG1);
                break;
            case 2:
                base_cfg->mutable_glog_config()->set_tty_verbosity(
                    util::protobuf::GLogConfig::DEBUG2);
                break;
            case 3:
                base_cfg->mutable_glog_config()->set_tty_verbosity(
                    util::protobuf::GLogConfig::DEBUG3);
                break;
        }
    }

    if (var_map.count("glog_file_verbose"))
    {
        switch (var_map["glog_file_verbose"].as<std::string>().size())
        {
            default:
            case 0:
                base_cfg->mutable_glog_config()->mutable_file_log()->set_verbosity(
                    util::protobuf::GLogConfig::VERBOSE);
                break;
            case 1:
                base_cfg->mutable_glog_config()->mutable_file_log()->set_verbosity(
                    util::protobuf::GLogConfig::DEBUG1);
                break;
            case 2:
                base_cfg->mutable_glog_config()->mutable_file_log()->set_verbosity(
                    util::protobuf::GLogConfig::DEBUG2);
                break;
            case 3:
                base_cfg->mutable_glog_config()->mutable_file_log()->set_verbosity(
                    util::protobuf::GLogConfig::DEBUG3);
                break;
        }
    }

    if (var_map.count("glog_file_dir"))
    {
        base_cfg->mutable_glog_config()->mutable_file_log()->set_file_dir(
            var_map["glog_file_dir"].as<std::string>());
    }
}

} // namespace middleware
} // namespace goby

#endif
