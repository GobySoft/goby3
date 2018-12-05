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
template <typename Config> class ProtobufConfigurator
{
  public:
    ProtobufConfigurator(int argc, char* argv[]);

    const Config& cfg()
    {
        if (!config_finalized_)
        {
            finalize_configuration(cfg_);
            common::ConfigReader::check_required_cfg(cfg_);
            config_finalized_ = true;
        }

        return cfg_;
    }
    const goby::protobuf::App3Config& app3_config() { return cfg().app(); }

  protected:
    // override to finalize the configuration at runtime
    virtual void finalize_configuration(Config& cfg) {}

  private:
    bool config_finalized_{false};
    Config cfg_;
};

template <typename Config>
ProtobufConfigurator<Config>::ProtobufConfigurator(int argc, char* argv[])
{
    //
    // read the configuration
    //
    boost::program_options::options_description od("Allowed options");
    boost::program_options::variables_map var_map;
    try
    {
        std::string application_name;

        // we will check it later
        bool check_required_cfg = false;
        common::ConfigReader::read_cfg(argc, argv, &cfg_, &application_name, &od, &var_map,
                                       check_required_cfg);

        cfg_.mutable_app()->set_name(application_name);
        // incorporate some parts of the AppBaseConfig that are common
        // with gobyd (e.g. Verbosity)
        merge_app_base_cfg(cfg_.mutable_app(), var_map);

        if (cfg_.app().debug_cfg())
        {
            std::cout << cfg_.DebugString() << std::endl;
            exit(EXIT_SUCCESS);
        }
    }
    catch (common::ConfigException& e)
    {
        // output all the available command line options
        if (e.error())
        {
            std::cerr << od << "\n";
            std::cerr << "Problem parsing command-line configuration: \n" << e.what() << "\n";
        }
        throw;
    }
}

} // namespace common
} // namespace goby

#endif
