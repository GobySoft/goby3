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

#include "goby/common/logger.h"
#include "goby/util/sci.h"

#include "goby/util/seawater/swstate.h"
#include "goby/util/seawater/depth.h"
#include "goby/util/seawater/salinity.h"

#include "frontseat.h"

namespace gpb = goby::moos::protobuf;
using goby::common::goby_time;
using goby::glog;
using namespace goby::common::logger;
using namespace goby::common::tcolor;

FrontSeatInterfaceBase::FrontSeatInterfaceBase(const iFrontSeatConfig& cfg)
    : cfg_(cfg),
      helm_state_(gpb::HELM_NOT_RUNNING),
      state_(gpb::INTERFACE_STANDBY),
      start_time_(goby::common::goby_time<double>()),
      last_frontseat_error_(gpb::ERROR_FRONTSEAT_NONE),
      last_helm_error_(gpb::ERROR_HELM_NONE)
{
    if(glog.is(DEBUG1))
    {
        signal_raw_from_frontseat.connect(boost::bind(&FrontSeatInterfaceBase::glog_raw, this, _1,
                                                      DIRECTION_FROM_FRONTSEAT));
        signal_raw_to_frontseat.connect(boost::bind(&FrontSeatInterfaceBase::glog_raw, this, _1,
                                                    DIRECTION_TO_FRONTSEAT));
    }

    if(!geodesy_.Initialise(cfg_.common().lat_origin(), cfg_.common().lon_origin()))
    {
        glog.is(DIE) && glog << "Failed to initialize MOOS Geodesy. Check LatOrigin and LongOrigin." << std::endl;
    }    
    
    glog_out_group_ = "FrontSeatInterfaceBase::raw::out";
    glog_in_group_ = "FrontSeatInterfaceBase::raw::in";
    
    goby::glog.add_group(glog_out_group_, goby::common::Colors::lt_magenta);
    goby::glog.add_group(glog_in_group_, goby::common::Colors::lt_blue);
}

void FrontSeatInterfaceBase::do_work()
{
    try
    {
        check_change_state();
        loop();
    }
    catch(FrontSeatException& e)
    {
        if(e.is_helm_error())
        {
            last_helm_error_ = e.helm_err();
            state_ = gpb::INTERFACE_HELM_ERROR;
            signal_state_change(state_);
        }
        else if(e.is_fs_error())
        {
            last_frontseat_error_ = e.fs_err();
            state_ = gpb::INTERFACE_FS_ERROR;
            signal_state_change(state_);
        }
        else
            throw;
    }
}
void FrontSeatInterfaceBase::check_change_state()
{
// check and change state
    gpb::InterfaceState previous_state = state_;
    switch(state_)
    {
        case gpb::INTERFACE_STANDBY: 
            if(frontseat_providing_data())
                state_ = gpb::INTERFACE_LISTEN;
            else
                check_error_states();
            break;
            
        case gpb::INTERFACE_LISTEN:
            if(frontseat_state() == gpb::FRONTSEAT_ACCEPTING_COMMANDS &&
               (helm_state() == gpb::HELM_DRIVE || !cfg_.require_helm()))
                state_ = gpb::INTERFACE_COMMAND;
            else
                check_error_states();
            break;
                
        case gpb::INTERFACE_COMMAND:
            if(frontseat_state() == gpb::FRONTSEAT_IN_CONTROL ||
               frontseat_state() == gpb::FRONTSEAT_IDLE)
                state_ = gpb::INTERFACE_LISTEN;
            else
                check_error_states();
            break;

        case gpb::INTERFACE_HELM_ERROR:
            // clear helm error states if appropriate
            if(helm_state() == gpb::HELM_DRIVE)
            {
                last_helm_error_ = gpb::ERROR_HELM_NONE;
                state_ = gpb::INTERFACE_STANDBY;
            }
            break;

        case gpb::INTERFACE_FS_ERROR:
            // clear frontseat error states if appropriate
            if(last_frontseat_error_ == gpb::ERROR_FRONTSEAT_NOT_CONNECTED &&
               frontseat_state() != gpb::FRONTSEAT_NOT_CONNECTED)
            {
                last_frontseat_error_ = gpb::ERROR_FRONTSEAT_NONE;
                state_ = gpb::INTERFACE_STANDBY;
            }
            else if(last_frontseat_error_ == gpb::ERROR_FRONTSEAT_NOT_PROVIDING_DATA
                    && frontseat_providing_data())
            {
                last_frontseat_error_ = gpb::ERROR_FRONTSEAT_NONE;
                state_ = gpb::INTERFACE_STANDBY;
            }
            
            break;
                
    }
    if(state_ != previous_state)
        signal_state_change(state_);
}

void FrontSeatInterfaceBase::check_error_states()
{
    // helm in park is always an error
    if(helm_state() == gpb::HELM_PARK)
        throw(FrontSeatException(gpb::ERROR_HELM_PARKED));
    // while in command, if the helm is not running, this is an error after
    // a configurable timeout, unless require_helm is false
    else if(cfg_.require_helm() &&
	    (helm_state() == gpb::HELM_NOT_RUNNING &&
            (state_ == gpb::INTERFACE_COMMAND ||
             (start_time_ + cfg_.helm_running_timeout() <
              goby_time<double>()))))
        throw(FrontSeatException(gpb::ERROR_HELM_NOT_RUNNING));

    // frontseat not connected is an error except in standby, it's only
    // an error after a timeout
    if(frontseat_state() == gpb::FRONTSEAT_NOT_CONNECTED &&
       (state_ != gpb::INTERFACE_STANDBY ||
        start_time_ + cfg_.frontseat_connected_timeout() <
        goby_time<double>()))
        throw(FrontSeatException(gpb::ERROR_FRONTSEAT_NOT_CONNECTED));
    // frontseat must always provide data in either the listen or command states
    else if(!frontseat_providing_data() && state_ != gpb::INTERFACE_STANDBY)
        throw(FrontSeatException(gpb::ERROR_FRONTSEAT_NOT_PROVIDING_DATA));

}

void FrontSeatInterfaceBase::glog_raw(const gpb::FrontSeatRaw& raw_msg, Direction direction)
{
    if (direction == DIRECTION_TO_FRONTSEAT)
        glog << group(glog_out_group_);
    else if(direction == DIRECTION_FROM_FRONTSEAT)
        glog << group(glog_in_group_);

    switch(raw_msg.type())
    {
        case gpb::FrontSeatRaw::RAW_ASCII:
            glog << raw_msg.raw() << "\n" << "^ "
                 << magenta << raw_msg.description()
                 << nocolor << std::endl;
            break;
        case gpb::FrontSeatRaw::RAW_BINARY:
            glog << raw_msg.raw().size() << "byte message\n" << "^ "
                 << magenta << raw_msg.description()
                 << nocolor << std::endl;
            break;
    }
};

void FrontSeatInterfaceBase::compute_missing(gpb::CTDSample* ctd_sample)
{
    double pressure_dbar = ctd_sample->pressure() / 10000;
    if(!ctd_sample->has_salinity())
    {
        double conductivity_mSiemens_cm = ctd_sample->conductivity() * 10;        
        double temperature_deg_C = ctd_sample->temperature();
        ctd_sample->set_salinity(SalinityCalculator::salinity(conductivity_mSiemens_cm,
                                                              temperature_deg_C,
                                                              pressure_dbar));
        ctd_sample->set_salinity_algorithm(
            gpb::CTDSample::UNESCO_44_PREKIN_AND_LEWIS_1980);
        
    }
    if(!ctd_sample->has_depth())
    {
        ctd_sample->set_depth(pressure2depth(pressure_dbar, ctd_sample->lat()));
    }
    if(!ctd_sample->has_sound_speed())
    {
        ctd_sample->set_sound_speed(goby::util::mackenzie_soundspeed(ctd_sample->temperature(),
                                                                      ctd_sample->salinity(),
                                                                      ctd_sample->depth()));

        ctd_sample->set_sound_speed_algorithm(
            gpb::CTDSample::MACKENZIE_1981);
    }
    if(!ctd_sample->has_density())
    {
	ctd_sample->set_density(density_anomaly(ctd_sample->salinity(),
                                                ctd_sample->temperature(),
                                                pressure_dbar) + 1000.0);
        
        ctd_sample->set_density_algorithm(
            gpb::CTDSample::UNESCO_38_MILLERO_AND_POISSON_1981);
    }
}

void FrontSeatInterfaceBase::compute_missing(gpb::NodeStatus* status)
{
    if(!status->has_name())
        status->set_name(cfg_.common().community());
    if(!status->has_modem_id())
	status->set_modem_id(cfg_.modem_id());
    
    if(!status->has_global_fix() && !status->has_local_fix())
    {
        glog.is(WARN) && glog << "Cannot 'compute_missing' on NodeStatus when global_fix and local_fix are both missing (cannot make up a position from nothing)!" << std::endl;
        return;
    }
    else if(!status->has_global_fix())
    {
        // compute global from local
        if(status->local_fix().has_z())
            status->mutable_global_fix()->set_depth(-status->local_fix().z());

        double lat, lon;
        geodesy_.UTM2LatLong(status->local_fix().x(), status->local_fix().y(),
                             lat, lon);
        status->mutable_global_fix()->set_lat(lat);
        status->mutable_global_fix()->set_lon(lon);        
    }
    else if(!status->has_local_fix())
    {
        // compute local from global
        if(status->global_fix().has_depth())
            status->mutable_local_fix()->set_z(-status->global_fix().depth());

        double x, y;
        geodesy_.LatLong2LocalUTM(status->global_fix().lat(), status->global_fix().lon(),
                                  y, x);
        status->mutable_local_fix()->set_x(x);
        status->mutable_local_fix()->set_y(y);        
    }
}
