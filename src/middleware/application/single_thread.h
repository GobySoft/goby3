// Copyright 2016-2022:
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

#ifndef GOBY_MIDDLEWARE_APPLICATION_SINGLE_THREAD_H
#define GOBY_MIDDLEWARE_APPLICATION_SINGLE_THREAD_H

#include <boost/units/systems/si.hpp>

#include "goby/middleware/coroner/coroner.h"
#include "goby/middleware/navigation/navigation.h"
#include "goby/middleware/terminate/terminate.h"

#include "goby/middleware/application/detail/interprocess_common.h"
#include "goby/middleware/application/groups.h"
#include "goby/middleware/application/interface.h"
#include "goby/middleware/application/thread.h"

#include "goby/middleware/transport/interprocess.h"
#include "goby/middleware/transport/intervehicle.h"

namespace goby
{
namespace middleware
{
/// \brief Implements an Application for a two layer middleware setup ([ intervehicle [ interprocess ] ]) based around InterVehicleForwarder without any interthread communications layer. This class isn't used directly by user applications, for that use a specific implementation, e.g. zeromq::SingleThreadApplication
///
/// \tparam Config Configuration type
/// \tparam InterProcessPortal the interprocess portal type to use (e.g. zeromq::InterProcessPortal)
template <class Config, template <class = NullTransporter> class InterProcessPortal>
class SingleThreadApplication : public goby::middleware::Application<Config>,
                                public Thread<Config, InterVehicleForwarder<InterProcessPortal<>>>
{
  private:
    using MainThread = Thread<Config, InterVehicleForwarder<InterProcessPortal<>>>;

    InterProcessPortal<> interprocess_;
    InterVehicleForwarder<InterProcessPortal<>> intervehicle_;

  public:
    /// \brief Construct the application calling loop() at the given frequency (double overload)
    ///
    /// \param loop_freq_hertz The frequency at which to attempt to call loop(), assuming the main thread isn't blocked handling transporter callbacks (e.g. subscribe callbacks). Zero or negative indicates loop() will never be called.
    SingleThreadApplication(double loop_freq_hertz = 0)
        : SingleThreadApplication(loop_freq_hertz * boost::units::si::hertz)
    {
    }

    /// \brief Construct the application calling loop() at the given frequency (boost::units overload)
    ///
    /// \param loop_freq The frequency at which to attempt to call loop(), assuming the main thread isn't blocked handling transporter callbacks (e.g. subscribe callbacks). Zero or negative indicates loop() will never be called.
    SingleThreadApplication(boost::units::quantity<boost::units::si::frequency> loop_freq)
        : MainThread(this->app_cfg(), loop_freq),
          interprocess_(
              detail::make_interprocess_config(this->app_cfg().interprocess(), this->app_name())),
          intervehicle_(interprocess_)
    {
        this->set_transporter(&intervehicle_);

        // handle goby_terminate request
        this->interprocess()
            .template subscribe<groups::terminate_request, protobuf::TerminateRequest>(
                [this](const protobuf::TerminateRequest& request) {
                    bool match = false;
                    protobuf::TerminateResponse resp;
                    std::tie(match, resp) =
                        terminate::check_terminate(request, this->app_cfg().app().name());
                    if (match)
                    {
                        this->interprocess().template publish<groups::terminate_response>(resp);
                        this->quit();
                    }
                });

        // handle goby_coroner request
        this->interprocess().template subscribe<groups::health_request, protobuf::HealthRequest>(
            [this](const protobuf::HealthRequest& request) {
                protobuf::ProcessHealth resp;
                resp.set_name(this->app_name());
                resp.set_pid(getpid());
                this->thread_health(*resp.mutable_main());
                this->interprocess().template publish<groups::health_response>(resp);
            });

        this->interprocess().template subscribe<goby::middleware::groups::datum_update>(
            [this](const protobuf::DatumUpdate& datum_update) {
                this->configure_geodesy(
                    {datum_update.datum().lat_with_units(), datum_update.datum().lon_with_units()});
            });

        this->interprocess().template publish<goby::middleware::groups::configuration>(
            this->app_cfg());
    }

    virtual ~SingleThreadApplication() {}

  protected:
    InterProcessPortal<>& interprocess() { return interprocess_; }
    InterVehicleForwarder<InterProcessPortal<>>& intervehicle() { return intervehicle_; }

    virtual void health(goby::middleware::protobuf::ThreadHealth& health) override
    {
        health.set_name(this->app_name());
        health.set_state(goby::middleware::protobuf::HEALTH__OK);
    }

    /// \brief Assume all required subscriptions are done in the Constructor or in initialize(). If this isn't the case, this method can be overridden
    virtual void post_initialize() override { interprocess().ready(); };

  private:
    void run() override { MainThread::run_once(); }
};

} // namespace middleware
} // namespace goby

#endif
