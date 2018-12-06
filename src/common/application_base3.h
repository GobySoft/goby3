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

#ifndef APPLICATIONBASE320161120H
#define APPLICATIONBASE320161120H

#include <chrono>
#include <csignal>
#include <iostream>
#include <sys/types.h>
#include <unistd.h>

#include <boost/format.hpp>

#include "goby/common/exception.h"

#include "goby/common/protobuf/app3.pb.h"

#include "goby/common/logger.h"
#include "goby/common/time3.h"

#include "configurator.h"
#include "core_helpers.h"

namespace goby
{
/// \brief Run a Goby application using the provided Configurator
/// blocks caller until ```__run()``` returns
/// \param cfgtor Subclass of ConfiguratorInterface used to configure the App
/// \tparam App ApplicationBase3 subclass
/// \return same as ```int main(int argc, char* argv)```
template <typename App>
int run(const goby::common::ConfiguratorInterface<typename App::ConfigType>& cfgtor);

/// \brief Shorthand for goby::run for Configurators that have a constructor that simply takes argc, argv, e.g. MyConfigurator(int argc, char* argv[]). Allows for backwards-compatibility pre-Configurator.
/// \param argc same as argc in ```int main(int argc, char* argv)```
/// \param argv same as argv in ```int main(int argc, char* argv)```
/// \tparam App ApplicationBase3 subclass
/// \tparam Configurator Configurator object that has a constructor such as ```Configurator(int argc, char* argv)```
/// \return same as ```int main(int argc, char* argv)```
template <typename App,
          typename Configurator = common::ProtobufConfigurator<typename App::ConfigType> >
int run(int argc, char* argv[])
{
    Configurator cfgtor(argc, argv);
    cfgtor.finalize();
    return run<App>(cfgtor);
}

namespace common
{
using NullStateMachine = int;

namespace internal
{
template <typename App, typename Config>
void __set_configuration(
    const goby::common::ConfiguratorInterface<typename App::ConfigType>& cfgtor)
{
    App::app_cfg_ = cfgtor.cfg();
    App::app3_base_configuration_ = cfgtor.app3_configuration();
}

// runs the application; returns the return value from App::__run()
template <typename App> int __run_application(App& app) { return app.__run(); }

// no state machine
template <typename App>
void __initialize_state_machine(
    App& app,
    typename std::enable_if<std::is_same<typename App::StateMachineType,
                                         goby::common::NullStateMachine>::value>::type* = 0)
{
}

// state machine
template <typename App>
void __initialize_state_machine(
    App& app,
    typename std::enable_if<!std::is_same<typename App::StateMachineType,
                                          goby::common::NullStateMachine>::value>::type* = 0);

// no state machine
template <typename App>
void __terminate_state_machine(
    App& app,
    typename std::enable_if<std::is_same<typename App::StateMachineType,
                                         goby::common::NullStateMachine>::value>::type* = 0)
{
}

// state machine
template <typename App>
void __terminate_state_machine(
    App& app,
    typename std::enable_if<!std::is_same<typename App::StateMachineType,
                                          goby::common::NullStateMachine>::value>::type* = 0);

} // namespace internal
} // namespace common

namespace common
{
template <typename Config, typename StateMachine = NullStateMachine> class ApplicationBase3
{
  public:
    ApplicationBase3();
    virtual ~ApplicationBase3()
    {
        goby::glog.is_debug2() && goby::glog << "ApplicationBase3: destructing cleanly"
                                             << std::endl;
    }

    using ConfigType = Config;
    using StateMachineType = StateMachine;

  protected:
    /// \brief Perform any initialize tasks that couldn't be done in the constructor
    ///
    /// For example, you now have access to the state machine
    virtual void initialize(){};

    /// \brief Runs continously until quit() is called
    virtual void run() = 0;

    /// \brief Perform any final actions before the destructor is called
    virtual void finalize(){};

    /// \brief Requests a clean exit.
    ///
    /// \param return_value The request return value
    virtual void quit(int return_value = 0)
    {
        alive_ = false;
        return_value_ = return_value;
    }

    /// \brief Accesses configuration object passed at launch
    const Config& app_cfg() const { return app_cfg_; }

    /// \brief Access the state machine if available (not in the constructor)
    StateMachine& state_machine()
    {
        static_assert(!std::is_same<StateMachine, NullStateMachine>(),
                      "No state machine defined for this Goby application");
        if (state_machine_)
            return *state_machine_;
        else
            throw(goby::Exception("State machine not available in the constructor."));
    }

  private:
    template <typename App> friend int ::goby::common::internal::__run_application(App& app);
    // main loop that exits on quit(); returns the desired return value
    int __run();

  private:
    // sets configuration (before Application construction)
    template <typename App, typename ConfigType>
    friend void ::goby::common::internal::__set_configuration(
        const goby::common::ConfiguratorInterface<typename App::ConfigType>& cfgtor);
    static Config app_cfg_;
    static goby::protobuf::App3Config app3_base_configuration_;

    bool alive_;
    int return_value_;

    // static here allows fout_ to live until program exit to log glog output
    static std::vector<std::unique_ptr<std::ofstream> > fout_;

    // set state machine after construction
    template <typename App>
    friend void ::goby::common::internal::__initialize_state_machine(
        App& app,
        typename std::enable_if<!std::is_same<typename App::StateMachineType,
                                              goby::common::NullStateMachine>::value>::type*);
    template <typename App>
    friend void ::goby::common::internal::__terminate_state_machine(
        App& app,
        typename std::enable_if<!std::is_same<typename App::StateMachineType,
                                              goby::common::NullStateMachine>::value>::type*);

    std::unique_ptr<StateMachine> state_machine_;
};
} // namespace common

} // namespace goby

template <typename Config, typename StateMachine>
std::vector<std::unique_ptr<std::ofstream> >
    goby::common::ApplicationBase3<Config, StateMachine>::fout_;

template <typename Config, typename StateMachine>
Config goby::common::ApplicationBase3<Config, StateMachine>::app_cfg_;

template <typename Config, typename StateMachine>
goby::protobuf::App3Config
    goby::common::ApplicationBase3<Config, StateMachine>::app3_base_configuration_;

template <typename Config, typename StateMachine>
goby::common::ApplicationBase3<Config, StateMachine>::ApplicationBase3() : alive_(true)
{
    using goby::glog;
    using namespace goby::common::logger;

    // set up the logger
    glog.set_name(app3_base_configuration_.name());
    glog.add_stream(static_cast<common::logger::Verbosity>(
                        app3_base_configuration_.glog_config().tty_verbosity()),
                    &std::cout);

    if (app3_base_configuration_.glog_config().show_gui())
        glog.enable_gui();

    fout_.resize(app3_base_configuration_.glog_config().file_log_size());
    for (int i = 0, n = app3_base_configuration_.glog_config().file_log_size(); i < n; ++i)
    {
        using namespace boost::posix_time;

        boost::format file_format(app3_base_configuration_.glog_config().file_log(i).file_name());
        file_format.exceptions(boost::io::all_error_bits ^
                               (boost::io::too_many_args_bit | boost::io::too_few_args_bit));

        std::string file_name = (file_format % to_iso_string(second_clock::universal_time()) %
                                 app3_base_configuration_.name())
                                    .str();
        std::string file_symlink = (file_format % "latest" % app3_base_configuration_.name()).str();

        glog.is(VERBOSE) && glog << "logging output to file: " << file_name << std::endl;

        fout_[i].reset(new std::ofstream(file_name.c_str()));

        if (!fout_[i]->is_open())
            glog.is(DIE) && glog << die
                                 << "cannot write glog output to requested file: " << file_name
                                 << std::endl;

        remove(file_symlink.c_str());
        int result = symlink(canonicalize_file_name(file_name.c_str()), file_symlink.c_str());
        if (result != 0)
            glog.is(WARN) &&
                glog << "Cannot create symlink to latest file. Continuing onwards anyway"
                     << std::endl;

        glog.add_stream(app3_base_configuration_.glog_config().file_log(i).verbosity(),
                        fout_[i].get());
    }

    if (!app3_base_configuration_.IsInitialized())
        throw(common::ConfigException("Invalid base configuration"));

    glog.is(DEBUG2) && glog << "ApplicationBase3: constructed with PID: " << getpid() << std::endl;
    glog.is(DEBUG1) && glog << "App name is " << app3_base_configuration_.name() << std::endl;
    glog.is(DEBUG2) && glog << "Configuration is: " << app_cfg_.DebugString() << std::endl;

    // set up simulation time
    if (app3_base_configuration_.simulation().time().use_sim_time())
    {
        goby::time::SimulatorSettings::using_sim_time = true;
        goby::time::SimulatorSettings::warp_factor =
            app3_base_configuration_.simulation().time().warp_factor();
        if (app3_base_configuration_.simulation().time().has_reference_microtime())
            goby::time::SimulatorSettings::reference_time =
                app3_base_configuration_.simulation().time().reference_microtime() *
                boost::units::si::micro * boost::units::si::seconds;
    }
}

template <typename Config, typename StateMachine>
int goby::common::ApplicationBase3<Config, StateMachine>::__run()
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
void goby::common::internal::__initialize_state_machine(
    App& app, typename std::enable_if<!std::is_same<typename App::StateMachineType,
                                                    goby::common::NullStateMachine>::value>::type*)
{
    app.state_machine_.reset(new typename App::StateMachineType(app));
    app.state_machine_->initiate();
}

template <typename App>
void goby::common::internal::__terminate_state_machine(
    App& app, typename std::enable_if<!std::is_same<typename App::StateMachineType,
                                                    goby::common::NullStateMachine>::value>::type*)
{
    app.state_machine_->terminate();
    app.state_machine_.reset();
}

template <typename App>
int goby::run(const goby::common::ConfiguratorInterface<typename App::ConfigType>& cfgtor)
{
    int return_value = 0;
    try
    {
        // set configuration
        goby::common::internal::__set_configuration<App, typename App::ConfigType>(cfgtor);

        // instantiate the application (with the configuration already set)
        App app;
        // initialize the state machine (if any)
        goby::common::internal::__initialize_state_machine<App>(app);
        // run the application
        return_value = goby::common::internal::__run_application<App>(app);
        // terminate the state machine (if any)
        goby::common::internal::__terminate_state_machine<App>(app);
    }
    catch (std::exception& e)
    {
        // some other exception
        std::cerr << "ApplicationBase3:: uncaught exception: " << e.what() << std::endl;
        return 2;
    }

    goby::glog.is_debug2() && goby::glog << "goby::run: exiting cleanly with code: " << return_value
                                         << std::endl;
    return return_value;
}

#endif
