// Copyright 2011-2020:
//   GobySoft, LLC (2013-)
//   Massachusetts Institute of Technology (2007-2014)
//   Community contributors (see AUTHORS file)
// File authors:
//   Toby Schneider <toby@gobysoft.org>
//   Russ Webber <russ@rw.id.au>
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

#ifndef APPLICATIONBASE320161120H
#define APPLICATIONBASE320161120H

#include <chrono>
#include <csignal>
#include <iostream>
#include <sys/types.h>
#include <unistd.h>

#include "goby/exception.h"
#include <boost/format.hpp>

#include "goby/middleware/application/configurator.h"
#include "goby/middleware/marshalling/detail/dccl_serializer_parser.h"
#include "goby/middleware/protobuf/app_config.pb.h"
#include "goby/time.h"
#include "goby/util/debug_logger.h"
#include "goby/util/geodesy.h"

namespace goby
{
/// \brief Run a Goby application using the provided Configurator.
///
/// Blocks caller until ```Application::quit()``` is called
/// \param cfgtor Subclass of ConfiguratorInterface used to configure the App
/// \tparam App Application subclass
/// \return same as ```int main(int argc, char* argv)```
template <typename App>
int run(const goby::middleware::ConfiguratorInterface<typename App::ConfigType>& cfgtor);

/// \brief Shorthand for goby::run for Configurators that have a constructor that simply takes argc, argv, e.g. MyConfigurator(int argc, char* argv[]). Allows for backwards-compatibility pre-Configurator.
/// \param argc same as argc in ```int main(int argc, char* argv)```
/// \param argv same as argv in ```int main(int argc, char* argv)```
/// \tparam App Application subclass
/// \tparam Configurator Configurator object that has a constructor such as ```Configurator(int argc, char* argv)```
/// \return same as ```int main(int argc, char* argv)```
template <typename App,
          typename Configurator = middleware::ProtobufConfigurator<typename App::ConfigType>>
int run(int argc, char* argv[])
{
    return run<App>(Configurator(argc, argv));
}

namespace middleware
{
/// \brief Base class for Goby applications. Generally you will want to use SingleThreadApplication or MultiThreadApplication rather than instantiating this class directly.
template <typename Config> class Application
{
  public:
    Application();
    virtual ~Application()
    {
        goby::glog.is_debug2() && goby::glog << "Application: destructing cleanly" << std::endl;
    }

    using ConfigType = Config;

  protected:
    /// \brief Perform any initialize tasks that couldn't be done in the constructor
    virtual void initialize(){};

    /// \brief Runs continuously until quit() is called
    virtual void run() = 0;

    /// \brief Perform any final cleanup actions just before the destructor is called
    virtual void finalize(){};

    /// \brief Requests a clean exit.
    ///
    /// \param return_value The request return value
    void quit(int return_value = 0)
    {
        alive_ = false;
        return_value_ = return_value;
    }

    /// \brief Accesses configuration object passed at launch
    const Config& app_cfg() { return *app_cfg_; }

    /// \brief Accesses the geodetic conversion tool if lat_origin and lon_origin were provided.
    const util::UTMGeodesy& geodesy()
    {
        if (geodesy_)
            return *geodesy_;
        else
            throw(goby::Exception("No lat_origin and lon_origin defined for requested UTMGeodesy"));
    }

    std::string app_name() { return app3_base_configuration_->name(); }

  private:
    template <typename App>
    friend int ::goby::run(
        const goby::middleware::ConfiguratorInterface<typename App::ConfigType>&);
    // main loop that exits on quit(); returns the desired return value
    int __run();

    void configure_logger();
    void configure_geodesy();

  private:
    // sets configuration (before Application construction)
    static std::unique_ptr<Config> app_cfg_;
    static std::unique_ptr<protobuf::AppConfig> app3_base_configuration_;

    bool alive_;
    int return_value_;

    // static here allows fout_ to live until program exit to log glog output
    static std::vector<std::unique_ptr<std::ofstream>> fout_;

    std::unique_ptr<goby::util::UTMGeodesy> geodesy_;
};
} // namespace middleware

} // namespace goby

template <typename Config>
std::vector<std::unique_ptr<std::ofstream>> goby::middleware::Application<Config>::fout_;

template <typename Config> std::unique_ptr<Config> goby::middleware::Application<Config>::app_cfg_;

template <typename Config>
std::unique_ptr<goby::middleware::protobuf::AppConfig>
    goby::middleware::Application<Config>::app3_base_configuration_;

template <typename Config> goby::middleware::Application<Config>::Application() : alive_(true)
{
    using goby::glog;

    configure_logger();
    if (app3_base_configuration_->has_geodesy())
        configure_geodesy();

    if (!app3_base_configuration_->IsInitialized())
        throw(middleware::ConfigException("Invalid base configuration"));

    glog.is_debug2() && glog << "Application: constructed with PID: " << getpid() << std::endl;
    glog.is_debug1() && glog << "App name is " << app3_base_configuration_->name() << std::endl;
    glog.is_debug2() && glog << "Configuration is: " << app_cfg_->DebugString() << std::endl;
}

template <typename Config> void goby::middleware::Application<Config>::configure_logger()
{
    using goby::glog;

    // set up the logger
    glog.set_name(app3_base_configuration_->name());
    glog.add_stream(static_cast<util::logger::Verbosity>(
                        app3_base_configuration_->glog_config().tty_verbosity()),
                    &std::cout);

    if (app3_base_configuration_->glog_config().show_gui())
        glog.enable_gui();

    fout_.resize(app3_base_configuration_->glog_config().file_log_size());
    for (int i = 0, n = app3_base_configuration_->glog_config().file_log_size(); i < n; ++i)
    {
        const auto& file_log = app3_base_configuration_->glog_config().file_log(i);
        std::string file_format_str;

        if (file_log.has_file_dir() && !file_log.file_dir().empty())
        {
            auto file_dir = file_log.file_dir();
            if (file_dir.back() != '/')
                file_dir += "/";
            file_format_str = file_dir + file_log.file_name();
        }
        else
        {
            file_format_str = file_log.file_name();
        }

        boost::format file_format(file_format_str);

        if (file_format_str.find("%1") == std::string::npos)
            glog.is_die() &&
                glog << "file_name string must contain \"%1%\" which is expanded to the current "
                        "application start time (e.g. 20190201T184925). Erroneous file_name is: "
                     << file_format_str << std::endl;

        file_format.exceptions(boost::io::all_error_bits ^
                               (boost::io::too_many_args_bit | boost::io::too_few_args_bit));

        std::string file_name =
            (file_format % goby::time::file_str() % app3_base_configuration_->name()).str();
        std::string file_symlink =
            (file_format % "latest" % app3_base_configuration_->name()).str();

        glog.is_verbose() && glog << "logging output to file: " << file_name << std::endl;

        fout_[i].reset(new std::ofstream(file_name.c_str()));

        if (!fout_[i]->is_open())
            glog.is_die() && glog << "cannot write glog output to requested file: " << file_name
                                  << std::endl;

        remove(file_symlink.c_str());
        int result = symlink(realpath(file_name.c_str(), NULL), file_symlink.c_str());
        if (result != 0)
            glog.is_warn() &&
                glog << "Cannot create symlink to latest file. Continuing onwards anyway"
                     << std::endl;

        glog.add_stream(app3_base_configuration_->glog_config().file_log(i).verbosity(),
                        fout_[i].get());
    }

    if (app3_base_configuration_->glog_config().show_dccl_log())
        goby::middleware::detail::DCCLSerializerParserHelperBase::setup_dlog();
}

template <typename Config> void goby::middleware::Application<Config>::configure_geodesy()
{
    geodesy_.reset(
        new goby::util::UTMGeodesy({app3_base_configuration_->geodesy().lat_origin_with_units(),
                                    app3_base_configuration_->geodesy().lon_origin_with_units()}));
}

template <typename Config> int goby::middleware::Application<Config>::__run()
{
    // block SIGWINCH (change window size) in all threads
    sigset_t signal_mask;
    sigemptyset(&signal_mask);
    sigaddset(&signal_mask, SIGWINCH);
    pthread_sigmask(SIG_BLOCK, &signal_mask, NULL);

    // continue to run while we are alive (quit() has not been called)

    this->initialize();
    while (alive_) { this->run(); }
    this->finalize();
    return return_value_;
}

template <typename App>
int goby::run(const goby::middleware::ConfiguratorInterface<typename App::ConfigType>& cfgtor)
{
    int return_value = 0;
    try
    {
        try
        {
            cfgtor.validate();
        }
        catch (middleware::ConfigException& e)
        {
            cfgtor.handle_config_error(e);
            return 1;
        }

        // simply print the configuration and exit
        if (cfgtor.app_configuration().debug_cfg())
        {
            std::cout << cfgtor.str() << std::endl;
            exit(EXIT_SUCCESS);
        }

        // set configuration
        App::app_cfg_.reset(new typename App::ConfigType(cfgtor.cfg()));
        App::app3_base_configuration_.reset(
            new goby::middleware::protobuf::AppConfig(cfgtor.app_configuration()));

        // set up simulation time
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

        // instantiate the application (with the configuration already set)
        App app;
        return_value = app.__run();
    }
    catch (std::exception& e)
    {
        // some other exception
        std::cerr << "Application:: uncaught exception: " << e.what() << std::endl;
        throw;
    }

    goby::glog.is_debug2() && goby::glog << "goby::run: exiting cleanly with code: " << return_value
                                         << std::endl;
    return return_value;
}

#endif
