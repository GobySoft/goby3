// Copyright 2013-2021:
//   GobySoft, LLC (2013-)
//   Massachusetts Institute of Technology (2007-2014)
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

#include <limits>    // for numeric_limits
#include <list>      // for operator==
#include <ostream>   // for operator<<
#include <stdexcept> // for out_of_range
#include <utility>   // for move

#include <boost/bind.hpp>                          // for bind_t, list...
#include <boost/function.hpp>                      // for function
#include <boost/signals2/expired_slot.hpp>         // for expired_slot
#include <boost/signals2/mutex.hpp>                // for mutex
#include <boost/units/absolute.hpp>                // for absolute
#include <boost/units/quantity.hpp>                // for quantity
#include <boost/units/systems/angle/degrees.hpp>   // for plane_angle
#include <boost/units/systems/si/length.hpp>       // for length
#include <boost/units/systems/si/mass_density.hpp> // for kilograms_pe...
#include <boost/units/unit.hpp>                    // for unit

#include "goby/exception.h"                             // for Exception
#include "goby/middleware/frontseat/exception.h"        // for Exception
#include "goby/middleware/protobuf/frontseat_data.pb.h" // for CTDSample
#include "goby/time/convert.h"                          // for SystemClock:...
#include "goby/time/system_clock.h"                     // for SystemClock
#include "goby/util/debug_logger/flex_ostream.h"        // for FlexOstream
#include "goby/util/debug_logger/flex_ostreambuf.h"     // for DEBUG1, DIE
#include "goby/util/debug_logger/logger_manipulators.h" // for operator<<
#include "goby/util/debug_logger/term_color.h"          // for magenta, noc...
#include "goby/util/seawater/depth.h"                   // for depth
#include "goby/util/seawater/salinity.h"                // for salinity
#include "goby/util/seawater/soundspeed.h"              // for mackenzie_so...
#include "goby/util/seawater/swstate.h"                 // for density_anomaly

#include "interface.h"

namespace gpb = goby::middleware::frontseat::protobuf;
using goby::glog;
using namespace goby::util::logger;
using namespace goby::util::tcolor;
using goby::time::MicroTime;

goby::middleware::frontseat::InterfaceBase::InterfaceBase(protobuf::Config cfg)
    : cfg_(std::move(cfg)),
      helm_state_(gpb::HELM_NOT_RUNNING),
      state_(gpb::INTERFACE_STANDBY),
      start_time_(goby::time::SystemClock::now<MicroTime>()),
      last_frontseat_error_(gpb::ERROR_FRONTSEAT_NONE),
      last_helm_error_(gpb::ERROR_HELM_NONE)
{
    if (glog.is(DEBUG1))
    {
        signal_raw_from_frontseat.connect(
            boost::bind(&InterfaceBase::glog_raw, this, _1, DIRECTION_FROM_FRONTSEAT));
        signal_raw_to_frontseat.connect(
            boost::bind(&InterfaceBase::glog_raw, this, _1, DIRECTION_TO_FRONTSEAT));
        // unlock
        glog << std::flush;
    }

    try
    {
        geodesy_.reset(new goby::util::UTMGeodesy(
            {cfg_.origin().lat_with_units(), cfg_.origin().lon_with_units()}));
    }
    catch (const goby::Exception& e)
    {
        glog.is(DIE) &&
            glog
                << "Failed to initialize UTMGeodesy. Check datum values (LatOrigin and LongOrigin)."
                << std::endl;
    }

    glog_out_group_ = "frontseat::InterfaceBase::raw::out";
    glog_in_group_ = "frontseat::InterfaceBase::raw::in";

    goby::glog.add_group(glog_out_group_, goby::util::Colors::lt_magenta);
    goby::glog.add_group(glog_in_group_, goby::util::Colors::lt_blue);
}

void goby::middleware::frontseat::InterfaceBase::do_work()
{
    try
    {
        check_change_state();
        loop();
    }
    catch (goby::middleware::frontseat::Exception& e)
    {
        if (e.is_helm_error())
        {
            last_helm_error_ = e.helm_err();
            state_ = gpb::INTERFACE_HELM_ERROR;
            signal_state_change(state_);
        }
        else if (e.is_fs_error())
        {
            last_frontseat_error_ = e.fs_err();
            state_ = gpb::INTERFACE_FS_ERROR;
            signal_state_change(state_);
        }
        else
            throw;
    }
}
void goby::middleware::frontseat::InterfaceBase::check_change_state()
{
    // check and change state
    gpb::InterfaceState previous_state = state_;
    switch (state_)
    {
        case gpb::INTERFACE_STANDBY:
            if (frontseat_providing_data())
                state_ = gpb::INTERFACE_LISTEN;
            else
                check_error_states();
            break;

        case gpb::INTERFACE_LISTEN:
            if (frontseat_state() == gpb::FRONTSEAT_ACCEPTING_COMMANDS &&
                (helm_state() == gpb::HELM_DRIVE || !cfg_.require_helm()))
                state_ = gpb::INTERFACE_COMMAND;
            else
                check_error_states();
            break;

        case gpb::INTERFACE_COMMAND:
            if (frontseat_state() == gpb::FRONTSEAT_IN_CONTROL ||
                frontseat_state() == gpb::FRONTSEAT_IDLE)
                state_ = gpb::INTERFACE_LISTEN;
            else
                check_error_states();
            break;

        case gpb::INTERFACE_HELM_ERROR:
            // clear helm error states if appropriate
            if (helm_state() == gpb::HELM_DRIVE)
            {
                last_helm_error_ = gpb::ERROR_HELM_NONE;
                state_ = gpb::INTERFACE_STANDBY;
            }
            break;

        case gpb::INTERFACE_FS_ERROR:
            // clear frontseat error states if appropriate
            if (last_frontseat_error_ == gpb::ERROR_FRONTSEAT_NOT_CONNECTED &&
                frontseat_state() != gpb::FRONTSEAT_NOT_CONNECTED)
            {
                last_frontseat_error_ = gpb::ERROR_FRONTSEAT_NONE;
                state_ = gpb::INTERFACE_STANDBY;
            }
            else if (last_frontseat_error_ == gpb::ERROR_FRONTSEAT_NOT_PROVIDING_DATA &&
                     frontseat_providing_data())
            {
                last_frontseat_error_ = gpb::ERROR_FRONTSEAT_NONE;
                state_ = gpb::INTERFACE_STANDBY;
            }

            break;
    }
    if (state_ != previous_state)
        signal_state_change(state_);
}

void goby::middleware::frontseat::InterfaceBase::check_error_states()
{
    // helm in park is always an error
    if (helm_state() == gpb::HELM_PARK)
        throw(goby::middleware::frontseat::Exception(gpb::ERROR_HELM_PARKED));
    // while in command, if the helm is not running, this is an error after
    // a configurable timeout, unless require_helm is false
    else if (cfg_.require_helm() &&
             (helm_state() == gpb::HELM_NOT_RUNNING &&
              (state_ == gpb::INTERFACE_COMMAND ||
               (start_time_ + cfg_.helm_running_timeout_with_units<MicroTime>() <
                goby::time::SystemClock::now<MicroTime>()))))
        throw(goby::middleware::frontseat::Exception(gpb::ERROR_HELM_NOT_RUNNING));

    // frontseat not connected is an error except in standby, it's only
    // an error after a timeout
    if (frontseat_state() == gpb::FRONTSEAT_NOT_CONNECTED &&
        (state_ != gpb::INTERFACE_STANDBY ||
         start_time_ + cfg_.frontseat_connected_timeout_with_units<MicroTime>() <
             goby::time::SystemClock::now<MicroTime>()))
        throw(goby::middleware::frontseat::Exception(gpb::ERROR_FRONTSEAT_NOT_CONNECTED));
    // frontseat must always provide data in either the listen or command states
    else if (!frontseat_providing_data() && state_ != gpb::INTERFACE_STANDBY)
        throw(goby::middleware::frontseat::Exception(gpb::ERROR_FRONTSEAT_NOT_PROVIDING_DATA));
}

void goby::middleware::frontseat::InterfaceBase::glog_raw(const gpb::Raw& raw_msg,
                                                          Direction direction)
{
    if (glog.is(DEBUG1))
    {
        if (direction == DIRECTION_TO_FRONTSEAT)
            glog << group(glog_out_group_);
        else if (direction == DIRECTION_FROM_FRONTSEAT)
            glog << group(glog_in_group_);

        switch (raw_msg.type())
        {
            case gpb::Raw::RAW_ASCII:
                glog << raw_msg.raw() << "\n"
                     << "^ " << magenta << raw_msg.description() << nocolor << "\n";
                break;
            case gpb::Raw::RAW_BINARY:
                glog << raw_msg.raw().size() << "byte message\n"
                     << "^ " << magenta << raw_msg.description() << nocolor << "\n";
                break;
        }
        // unlock
        glog << std::flush;
    }
};

void goby::middleware::frontseat::InterfaceBase::compute_missing(gpb::CTDSample* ctd_sample)
{
    if (!ctd_sample->has_salinity())
    {
        ctd_sample->set_salinity_with_units(goby::util::seawater::salinity(
            ctd_sample->conductivity_with_units(), ctd_sample->temperature_with_units(),
            ctd_sample->pressure_with_units()));
        ctd_sample->set_salinity_algorithm(gpb::CTDSample::UNESCO_44_PREKIN_AND_LEWIS_1980);
    }
    if (!ctd_sample->global_fix().has_depth())
    {
        // should always be true, but if() to workaround clang-analyzer false positive ("Called C++ object pointer is null")
        if (ctd_sample->mutable_global_fix())
            ctd_sample->mutable_global_fix()->set_depth_with_units(goby::util::seawater::depth(
                ctd_sample->pressure_with_units(), ctd_sample->global_fix().lat_with_units()));
    }
    if (!ctd_sample->has_sound_speed())
    {
        try
        {
            ctd_sample->set_sound_speed_with_units(goby::util::seawater::mackenzie_soundspeed(
                ctd_sample->temperature_with_units(), ctd_sample->salinity_with_units(),
                ctd_sample->global_fix().depth_with_units()));
        }
        catch (std::out_of_range& e)
        {
            glog.is_warn() && glog << "Out of range error calculating soundspeed: " << e.what()
                                   << std::endl;
            ctd_sample->set_sound_speed(std::numeric_limits<double>::quiet_NaN());
        }

        ctd_sample->set_sound_speed_algorithm(gpb::CTDSample::MACKENZIE_1981);
    }
    if (!ctd_sample->has_density())
    {
        ctd_sample->set_density_with_units(
            goby::util::seawater::density_anomaly(ctd_sample->salinity_with_units(),
                                                  ctd_sample->temperature_with_units(),
                                                  ctd_sample->pressure_with_units()) +
            1000.0 * boost::units::si::kilograms_per_cubic_meter);

        ctd_sample->set_density_algorithm(gpb::CTDSample::UNESCO_38_MILLERO_AND_POISSON_1981);
    }
}

void goby::middleware::frontseat::InterfaceBase::compute_missing(gpb::NodeStatus* status)
{
    if (!status->has_name())
        status->set_name(cfg_.name());

    if (!status->has_type())
        status->set_type(cfg_.type());

    if (!status->has_time())
        status->set_time_with_units(goby::time::SystemClock::now<goby::time::SITime>());

    if (!status->has_global_fix() && !status->has_local_fix())
    {
        glog.is(WARN) &&
            glog << "Cannot 'compute_missing' on NodeStatus when global_fix and local_fix are both "
                    "missing (cannot make up a position from nothing)!"
                 << std::endl;
        return;
    }
    else if (!status->has_global_fix())
    {
        // compute global from local
        if (status->local_fix().has_z())
            status->mutable_global_fix()->set_depth_with_units(-status->local_fix().z_with_units());

        auto ll = geodesy_->convert(
            {status->local_fix().x_with_units(), status->local_fix().y_with_units()});
        status->mutable_global_fix()->set_lat_with_units(ll.lat);
        status->mutable_global_fix()->set_lon_with_units(ll.lon);
    }
    else if (!status->has_local_fix())
    {
        // compute local from global
        if (status->global_fix().has_depth())
            status->mutable_local_fix()->set_z_with_units(-status->global_fix().depth_with_units());

        auto xy = geodesy_->convert(
            {status->global_fix().lat_with_units(), status->global_fix().lon_with_units()});
        status->mutable_local_fix()->set_x_with_units(xy.x);
        status->mutable_local_fix()->set_y_with_units(xy.y);
    }
}

void goby::middleware::frontseat::InterfaceBase::update_utm_datum(double lat_origin,
								  double lon_origin)
{
    boost::units::quantity<boost::units::degree::plane_angle> lat =
        lat_origin * boost::units::degree::degrees;
    boost::units::quantity<boost::units::degree::plane_angle> lon =
        lon_origin * boost::units::degree::degrees;

    geodesy_.reset(new goby::util::UTMGeodesy({lat, lon}));
}
