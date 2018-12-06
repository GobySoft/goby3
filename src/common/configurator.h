// Copyright 2009-2018 Toby Schneider (http://gobysoft.org/index.wt/people/toby)
//                     GobySoft, LLC (2013-)
//                     Massachusetts Institute of Technology (2007-2014)
//                     Community contributors (see AUTHORS file)
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

#ifndef CONFIGURATOR_20181205H
#define CONFIGURATOR_20181205H

#include "goby/common/configuration_reader.h"
#include "goby/common/core_helpers.h"
#include "goby/common/protobuf/app3.pb.h"

namespace goby
{
namespace common
{
template <typename Config> class ConfiguratorInterface
{
  public:
    void finalize()
    {
        if (!config_finalized_)
        {
            try
            {
                finalize_cfg();
            }
            catch (common::ConfigException& e)
            {
                handle_config_error(e);
                throw;
            }
            config_finalized_ = true;
        }

        if (app3_configuration().debug_cfg())
        {
            std::cout << cfg().DebugString() << std::endl;
            exit(EXIT_SUCCESS);
        }
    }

    const Config& cfg() const
    {
        check_finalized();
        return const_cfg();
    }

    const goby::protobuf::App3Config& app3_configuration() const
    {
        check_finalized();
        return const_app3_configuration();
    }

  private:
    void check_finalized() const
    {
        if (!config_finalized_)
            throw(
                common::ConfigException("Configuration is not finalized (call finalize() first)"));
    }

    virtual void finalize_cfg() {}
    virtual const Config& const_cfg() const = 0;
    virtual const goby::protobuf::App3Config& const_app3_configuration() const = 0;

    virtual void handle_config_error(common::ConfigException& e) {}

  private:
    bool config_finalized_{false};
};

/// Implementation of ConfiguratorInterface for Google Protocol buffers
template <typename Config> class ProtobufConfigurator : public ConfiguratorInterface<Config>
{
  public:
    ProtobufConfigurator(int argc, char* argv[]);

  protected:
    // subclass can override to finalize the configuration at runtime
    virtual void finalize_configuration(Config& cfg) {}

  private:
    void finalize_cfg() override
    {
        finalize_configuration(cfg_);
        common::ConfigReader::check_required_cfg(cfg_);
    }

    const Config& const_cfg() const override { return cfg_; }
    const goby::protobuf::App3Config& const_app3_configuration() const override
    {
        return cfg_.app();
    }

    void handle_config_error(common::ConfigException& e) override
    {
        // output all the available command line options
        if (e.error())
            std::cerr
                << "Problem parsing configuration: use --help or --example_config for more help."
                << std::endl;
    }

  private:
    Config cfg_;
    boost::program_options::options_description od_{"Allowed options"};
};

template <typename Config>
ProtobufConfigurator<Config>::ProtobufConfigurator(int argc, char* argv[])
{
    //
    // read the configuration
    //
    boost::program_options::variables_map var_map;
    try
    {
        std::string application_name;

        // we will check it later
        bool check_required_cfg = false;
        common::ConfigReader::read_cfg(argc, argv, &cfg_, &application_name, &od_, &var_map,
                                       check_required_cfg);

        cfg_.mutable_app()->set_name(application_name);
        // incorporate some parts of the AppBaseConfig that are common
        // with gobyd (e.g. Verbosity)
        merge_app_base_cfg(cfg_.mutable_app(), var_map);
    }
    catch (common::ConfigException& e)
    {
        handle_config_error(e);
        throw;
    }
}

} // namespace common
} // namespace goby

#endif
