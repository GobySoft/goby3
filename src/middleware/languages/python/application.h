// Copyright 2026:
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

#ifndef GOBY_MIDDLEWARE_LANGUAGES_PYTHON_APPLICATION_H
#define GOBY_MIDDLEWARE_LANGUAGES_PYTHON_APPLICATION_H

#include <csignal>   // for signal, raise, sig_atomic_t
#include <cstdlib>   // for exit
#include <exception> // for exception_ptr
#include <iostream>  // for cout
#include <memory>    // for unique_ptr
#include <string>    // for string
#include <vector>    // for vector

#include <google/protobuf/text_format.h>

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "goby/exception.h"
#include "goby/middleware/application/configurator.h"
#include "goby/middleware/application/detail/simulation_time.h"
#include "goby/middleware/application/interface.h"
#include "goby/middleware/group.h"
#include "goby/middleware/languages/common/interface.h"
#include "goby/middleware/marshalling/interface.h"
#include "goby/time.h"

namespace goby
{
namespace middleware
{
/// \brief Support for writing Goby applications in Python
///
/// Python applications are written as a subclass of the application class exported by the
/// generated extension module, mirroring how C++ applications subclass SingleThreadApplication.
/// The generated module and its C++ glue code are produced from the same interface.yml file used
/// by the Julia bindings (see goby/middleware/languages/julia/application.h).
namespace python
{
namespace py = pybind11;

using goby::middleware::languages::Identifier;
using goby::middleware::languages::PubSubLayer;

namespace detail
{
/// \brief Hands marshalled bytes to a Python callable, acquiring the GIL first
///
/// Called from the generated subscribe glue, which runs on whichever thread is servicing the
/// transporter callback.
inline void invoke_callback(const py::object& callback, const std::vector<char>& bytes)
{
    py::gil_scoped_acquire gil;
    callback(py::bytes(bytes.data(), bytes.size()));
}

/// \brief State used to turn SIGINT/SIGTERM into a clean Application::quit()
///
/// Only plain data is touched from the handler itself; quit_fn writes the Application's alive_
/// flag, which __run() checks on each pass.
struct SignalState
{
    void (*quit_fn)(void*) = nullptr;
    void* quit_ctx = nullptr;
    volatile std::sig_atomic_t requested = 0;
};

inline SignalState& signal_state()
{
    static SignalState state;
    return state;
}

inline void signal_handler(int signum)
{
    SignalState& state = signal_state();

    if (state.quit_fn != nullptr && state.requested == 0)
    {
        state.requested = 1;
        state.quit_fn(state.quit_ctx);
    }
    else
    {
        // a second signal means the application isn't unwinding (e.g. it is blocked waiting on
        // data with no loop frequency set), so fall back to the default disposition
        std::signal(signum, SIG_DFL);
        std::raise(signum);
    }
}

/// \brief Installs the quit-on-signal handlers for the duration of a run() call
///
/// This deliberately displaces the Python interpreter's own SIGINT handler while the C++ event
/// loop is running: with the GIL released the interpreter would not act on a KeyboardInterrupt
/// until the next callback, raising it from inside Goby's C++ call stack.
class SignalGuard
{
  public:
    SignalGuard(void (*quit_fn)(void*), void* quit_ctx)
    {
        SignalState& state = signal_state();
        state.quit_fn = quit_fn;
        state.quit_ctx = quit_ctx;
        state.requested = 0;

        previous_sigint_ = std::signal(SIGINT, &signal_handler);
        previous_sigterm_ = std::signal(SIGTERM, &signal_handler);
    }

    ~SignalGuard()
    {
        if (previous_sigint_ != SIG_ERR)
            std::signal(SIGINT, previous_sigint_);
        if (previous_sigterm_ != SIG_ERR)
            std::signal(SIGTERM, previous_sigterm_);

        SignalState& state = signal_state();
        state.quit_fn = nullptr;
        state.quit_ctx = nullptr;
    }

    SignalGuard(const SignalGuard&) = delete;
    SignalGuard& operator=(const SignalGuard&) = delete;

  private:
    void (*previous_sigint_)(int){SIG_ERR};
    void (*previous_sigterm_)(int){SIG_ERR};
};

} // namespace detail

/// \brief Base class for the generated C++ glue class, forwarding the Goby virtual methods to Python
///
/// \tparam AppBase the application class named by application.cpp_type in interface.yml, e.g.
///         goby::zeromq::SingleThreadApplication<Config>
template <typename AppBase> class Application : public AppBase
{
  public:
    using AppBase::AppBase;

    using Hook = std::function<void()>;

    /// \brief Called by ApplicationWrapper immediately after construction
    void set_hooks(Hook loop, Hook initialize, Hook finalize)
    {
        loop_ = std::move(loop);
        initialize_ = std::move(initialize);
        finalize_ = std::move(finalize);
    }

  protected:
    void loop() override
    {
        if (loop_)
            loop_();
    }

    // SingleThreadApplication inherits initialize()/finalize() from both Application and Thread.
    // Only one set is ever invoked for a given application type, but guard anyway so that a
    // Python override cannot be called twice.
    void initialize() override
    {
        if (initialize_ && !initialize_called_)
        {
            initialize_called_ = true;
            initialize_();
        }
    }

    void finalize() override
    {
        if (finalize_ && !finalize_called_)
        {
            finalize_called_ = true;
            finalize_();
        }
    }

  private:
    Hook loop_;
    Hook initialize_;
    Hook finalize_;
    bool initialize_called_{false};
    bool finalize_called_{false};
};

/// \brief The class exposed to Python; user application classes subclass this
///
/// \tparam App the generated glue class, which derives from python::Application
template <typename App> class ApplicationWrapper
{
  public:
    /// \brief Constructs the underlying Goby application
    ///
    /// configure() must have been called first (goby.run does this), as the Goby application
    /// reads its configuration during construction.
    ///
    /// \param loop_frequency_hertz frequency at which to call loop(); zero means never
    ApplicationWrapper(double loop_frequency_hertz = 0)
    {
        if (!App::app_cfg_)
            throw(goby::Exception(
                "Goby configuration has not been read. Applications must be started with "
                "goby.run(), which parses the command line before constructing the application."));

        app_ptr_.reset(new App(loop_frequency_hertz));
        app_ptr_->set_hooks([this]() { this->loop(); }, [this]() { this->initialize(); },
                            [this]() { this->finalize(); });
    }

    virtual ~ApplicationWrapper() = default;

    ApplicationWrapper(const ApplicationWrapper&) = delete;
    ApplicationWrapper& operator=(const ApplicationWrapper&) = delete;

    /// \brief Override in Python to do work at loop_frequency Hz
    virtual void loop()
    {
        // mirrors the diagnostic from Thread::loop()
        throw(goby::Exception("loop() must be overridden when loop_frequency is non-zero"));
    }

    /// \brief Override in Python for work that can't be done in the constructor
    virtual void initialize() {}

    /// \brief Override in Python for cleanup just before the application exits
    virtual void finalize() {}

    /// \brief Reads the command line into the application configuration
    ///
    /// Mirrors the configuration handling in goby::run, so Python applications accept the same
    /// command line as C++ applications (--help, --example_config, -v, per-field overrides, ...).
    ///
    /// \param argv the full command line, including argv[0]
    /// \return the parsed configuration, serialized as Protobuf
    /// \throw goby::middleware::ConfigException translated to goby.ConfigError in Python
    static py::bytes configure(const std::vector<std::string>& argv)
    {
        using ConfigType = typename App::ConfigType;

        std::vector<char*> argv_c;
        argv_c.reserve(argv.size() + 1);
        for (const auto& arg : argv) argv_c.push_back(const_cast<char*>(arg.c_str()));
        argv_c.push_back(nullptr);

        ProtobufConfigurator<ConfigType> protobuf_cfgtor(static_cast<int>(argv.size()),
                                                         argv_c.data());
        // ProtobufConfigurator narrows the access of these overrides, so go through the
        // interface, as goby::run does
        const ConfiguratorInterface<ConfigType>& cfgtor = protobuf_cfgtor;

        try
        {
            cfgtor.validate();
        }
        catch (middleware::ConfigException& e)
        {
            cfgtor.handle_config_error(e);
            throw;
        }

        // simply print the configuration and exit
        if (cfgtor.app_configuration().debug_cfg())
        {
            std::cout << cfgtor.str() << std::endl;
            std::exit(EXIT_SUCCESS);
        }

        App::app_cfg_.reset(new ConfigType(cfgtor.cfg()));
        App::app3_base_configuration_.reset(
            new goby::middleware::protobuf::AppConfig(cfgtor.app_configuration()));

        goby::middleware::detail::configure_simulation_time(*App::app3_base_configuration_);

        return set_configuration(cfgtor.cfg());
    }

    /// \brief Reads the application configuration from a Protobuf TextFormat string
    ///
    /// Intended for embedding a Goby application in a larger Python program, and for tests that
    /// shouldn't need a command line or a configuration file.
    ///
    /// \throw goby::middleware::ConfigException translated to goby.ConfigError in Python
    static py::bytes configure_from_text(const std::string& config_text)
    {
        typename App::ConfigType cfg;

        google::protobuf::TextFormat::Parser parser;
        goby::util::FlexOStreamErrorCollector error_collector(config_text);
        parser.RecordErrorsTo(&error_collector);
        parser.AllowPartialMessage(false);

        if (!parser.ParseFromString(config_text, &cfg))
            throw(middleware::ConfigException(
                "Failed to parse the configuration as Protobuf TextFormat"));

        return set_configuration(cfg);
    }

    /// \brief Reads the application configuration from a serialized Protobuf message
    ///
    /// \throw goby::middleware::ConfigException translated to goby.ConfigError in Python
    static py::bytes configure_from_serialized(const std::string& config_bytes)
    {
        typename App::ConfigType cfg;

        if (!cfg.ParseFromString(config_bytes))
            throw(middleware::ConfigException("Failed to parse the serialized configuration"));

        return set_configuration(cfg);
    }

    /// \brief Runs the Goby event loop until quit() is called or the application is terminated
    ///
    /// The GIL is released for the duration, and reacquired for each callback into Python.
    ///
    /// \return the application return value
    int run()
    {
        detail::SignalGuard signal_guard(&ApplicationWrapper::signal_quit, this);
        py::gil_scoped_release release_gil;
        return app_ptr_->__run();
    }

    /// \brief Requests a clean exit
    void quit(int return_value = 0) { app_ptr_->quit(return_value); }

    /// \brief The application name, as set by --app_name or derived from argv[0]
    std::string app_name() { return app_ptr_->app_name(); }

    /// \brief The configuration the application was started with, serialized as Protobuf
    py::bytes cfg_serialized()
    {
        std::string serialized;
        app_ptr_->app_cfg().SerializeToString(&serialized);
        return py::bytes(serialized);
    }

    void set_loop_frequency_hertz(double loop_frequency_hertz)
    {
        app_ptr_->set_loop_frequency_hertz(loop_frequency_hertz);
    }

    /// \brief Publishes already-marshalled bytes; dispatched by the generated glue code
    void publish(int layer, const std::string& type_name, int scheme, const std::string& group,
                 const std::string& data)
    {
        app_ptr_->publish(Identifier{static_cast<PubSubLayer>(layer), type_name, scheme, group},
                          data);
    }

    /// \brief Subscribes a Python callable; dispatched by the generated glue code
    void subscribe(int layer, const std::string& type_name, int scheme, const std::string& group,
                   py::object callback)
    {
        app_ptr_->subscribe(Identifier{static_cast<PubSubLayer>(layer), type_name, scheme, group},
                            std::move(callback));
    }

  private:
    /// \brief Common tail of the configure() entry points: validate, publish to the statics the
    ///        Application reads at construction, and hand the configuration back to Python
    static py::bytes set_configuration(const typename App::ConfigType& cfg)
    {
        ConfigReader::check_required_cfg(cfg, cfg.app().binary());

        App::app_cfg_.reset(new typename App::ConfigType(cfg));
        App::app3_base_configuration_.reset(new goby::middleware::protobuf::AppConfig(cfg.app()));

        goby::middleware::detail::configure_simulation_time(*App::app3_base_configuration_);

        std::string serialized;
        cfg.SerializeToString(&serialized);
        return py::bytes(serialized);
    }

    static void signal_quit(void* self)
    {
        static_cast<ApplicationWrapper*>(self)->app_ptr_->quit();
    }

  private:
    std::unique_ptr<App> app_ptr_;
};

/// \brief Lets Python subclasses override the Goby virtual methods
template <typename App> class ApplicationWrapperTrampoline : public ApplicationWrapper<App>
{
  public:
    using ApplicationWrapper<App>::ApplicationWrapper;

    void loop() override { PYBIND11_OVERRIDE(void, ApplicationWrapper<App>, loop, ); }
    void initialize() override { PYBIND11_OVERRIDE(void, ApplicationWrapper<App>, initialize, ); }
    void finalize() override { PYBIND11_OVERRIDE(void, ApplicationWrapper<App>, finalize, ); }
};

/// \brief Defines the contents of the generated extension module
template <typename App>
inline void define_python_module(py::module_& m, const std::string& app_name)
{
    // ConfigException is raised as goby.ConfigError so that all generated modules share one
    // exception type, which goby.run() catches
    py::register_exception_translator(
        [](std::exception_ptr p)
        {
            try
            {
                if (p)
                    std::rethrow_exception(p);
            }
            catch (const middleware::ConfigException& e)
            {
                py::object goby_module = py::module_::import("goby");
                PyErr_SetString(goby_module.attr("ConfigError").ptr(), e.what());
            }
        });

    py::class_<ApplicationWrapper<App>, ApplicationWrapperTrampoline<App>>(m, "_ApplicationBase")
        .def(py::init<double>(), py::arg("loop_frequency_hertz") = 0)
        .def("loop", &ApplicationWrapper<App>::loop)
        .def("initialize", &ApplicationWrapper<App>::initialize)
        .def("finalize", &ApplicationWrapper<App>::finalize)
        .def("quit", &ApplicationWrapper<App>::quit, py::arg("return_value") = 0)
        .def("app_name", &ApplicationWrapper<App>::app_name)
        .def("_run", &ApplicationWrapper<App>::run)
        .def("_publish", &ApplicationWrapper<App>::publish, py::arg("layer"), py::arg("type_name"),
             py::arg("scheme"), py::arg("group"), py::arg("data"))
        .def("_subscribe", &ApplicationWrapper<App>::subscribe, py::arg("layer"),
             py::arg("type_name"), py::arg("scheme"), py::arg("group"), py::arg("callback"))
        .def("_cfg_serialized", &ApplicationWrapper<App>::cfg_serialized)
        .def("_set_loop_frequency_hertz", &ApplicationWrapper<App>::set_loop_frequency_hertz)
        .def_static("_configure", &ApplicationWrapper<App>::configure, py::arg("argv"))
        .def_static("_configure_from_text", &ApplicationWrapper<App>::configure_from_text,
                    py::arg("config_text"))
        .def_static("_configure_from_serialized",
                    &ApplicationWrapper<App>::configure_from_serialized, py::arg("config_bytes"));

    m.attr("_GOBY_APPLICATION_NAME") = app_name;
}

} // namespace python
} // namespace middleware
} // namespace goby

// Macros for use by the autogenerated code to define the C++ side of the pub/sub setup.
//
// GROUP is the C++ group expression as written in interface.yml, used for the (compile time)
// Goby call. GROUP_KEY is that same expression as a string, and is what the Python side names
// the group by: the group's own runtime name is not necessarily the same (a Group declared as
// `constexpr Group nav{"navigation"}` is written `groups::nav` in the interface file), and the
// interface file is the only name Python has.
#define GOBY_PYTHON_IF_PUBLICATION(SCHEME, LAYER_ENUM, LAYER_FUNCTION, GROUP, GROUP_KEY, TYPE)   \
    if (id == goby::middleware::python::Identifier{                                              \
                  goby::middleware::python::PubSubLayer::LAYER_ENUM, TYPE::descriptor()->name(), \
                  goby::middleware::MarshallingScheme::SCHEME, GROUP_KEY})                       \
    {                                                                                            \
        decltype(data.end()) actual_end;                                                         \
        auto msg = goby::middleware::SerializerParserHelper<                                     \
            TYPE, goby::middleware::MarshallingScheme::SCHEME>::parse(data.begin(), data.end(),  \
                                                                      actual_end);               \
        LAYER_FUNCTION().publish<GROUP>(msg);                                                    \
        return;                                                                                  \
    }

#define GOBY_PYTHON_IF_SUBSCRIPTION(SCHEME, LAYER_ENUM, LAYER_FUNCTION, GROUP, GROUP_KEY, TYPE)  \
    if (id == goby::middleware::python::Identifier{                                              \
                  goby::middleware::python::PubSubLayer::LAYER_ENUM, TYPE::descriptor()->name(), \
                  goby::middleware::MarshallingScheme::SCHEME, GROUP_KEY})                       \
    {                                                                                            \
        LAYER_FUNCTION().subscribe<GROUP>(                                                       \
            [callback](const TYPE& pb)                                                           \
            {                                                                                    \
                std::vector<char> bytes = goby::middleware::SerializerParserHelper<              \
                    TYPE, goby::middleware::MarshallingScheme::SCHEME>::serialize(pb);           \
                goby::middleware::python::detail::invoke_callback(callback, bytes);              \
            });                                                                                  \
        return;                                                                                  \
    }

#define GOBY_PYTHON_FAIL(PUBLISH_OR_SUBSCRIBE)                                              \
    goby::glog.is_die() &&                                                                  \
        goby::glog << PUBLISH_OR_SUBSCRIBE " not defined for these parameters: [" << id     \
                   << "]. Please include in interface.yml and re-generate to include them." \
                   << std::endl;

// used to stringify application name
#define GOBY_PYTHON_QUOTE(name) #name
#define GOBY_PYTHON_DEFINE_MODULE(APPLICATION_NAME, MODULE_NAME)          \
    PYBIND11_MODULE(MODULE_NAME, goby_python_module)                      \
    {                                                                     \
        goby::middleware::python::define_python_module<APPLICATION_NAME>( \
            goby_python_module, GOBY_PYTHON_QUOTE(APPLICATION_NAME));     \
    }

#endif
