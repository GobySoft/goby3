// Copyright 2013-2020:
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

#ifndef FrontSeatBase20130220H
#define FrontSeatBase20130220H

#include <boost/signals2.hpp>

#include "goby/time.h"

#include "goby/moos/moos_geodesy.h"

#include "goby/moos/frontseat/frontseat_exception.h"
#include "goby/moos/protobuf/frontseat.pb.h"
#include "goby/moos/protobuf/frontseat_config.pb.h"

namespace goby
{
namespace apps
{
namespace moos
{
class FrontSeatLegacyTranslator;
}
} // namespace apps
namespace moos
{
class FrontSeatInterfaceBase
{
  public:
    FrontSeatInterfaceBase(const goby::apps::moos::protobuf::iFrontSeatConfig& cfg);

    virtual ~FrontSeatInterfaceBase() {}

    virtual void send_command_to_frontseat(const goby::moos::protobuf::CommandRequest& command) = 0;
    virtual void
    send_data_to_frontseat(const goby::moos::protobuf::FrontSeatInterfaceData& data) = 0;
    virtual void send_raw_to_frontseat(const goby::moos::protobuf::FrontSeatRaw& data) = 0;

    virtual goby::moos::protobuf::FrontSeatState frontseat_state() const = 0;
    virtual bool frontseat_providing_data() const = 0;

    void set_helm_state(goby::moos::protobuf::HelmState state) { helm_state_ = state; }
    goby::moos::protobuf::HelmState helm_state() const { return helm_state_; }
    goby::moos::protobuf::InterfaceState state() const { return state_; }

    void do_work();

    goby::moos::protobuf::FrontSeatInterfaceStatus status()
    {
        goby::moos::protobuf::FrontSeatInterfaceStatus s;
        s.set_state(state_);
        s.set_frontseat_state(frontseat_state());
        s.set_helm_state(helm_state_);
        if (last_helm_error_ != goby::moos::protobuf::ERROR_HELM_NONE)
            s.set_helm_error(last_helm_error_);
        if (last_frontseat_error_ != goby::moos::protobuf::ERROR_FRONTSEAT_NONE)
            s.set_frontseat_error(last_frontseat_error_);
        return s;
    }

    // Called at the AppTick frequency of iFrontSeat
    // Here is where you can process incoming data
    virtual void loop() = 0;

    // Signals that iFrontseat connects to
    // call this with data from the Frontseat
    boost::signals2::signal<void(const goby::moos::protobuf::CommandResponse& data)>
        signal_command_response;
    boost::signals2::signal<void(const goby::moos::protobuf::FrontSeatInterfaceData& data)>
        signal_data_from_frontseat;
    boost::signals2::signal<void(const goby::moos::protobuf::FrontSeatRaw& data)>
        signal_raw_from_frontseat;
    boost::signals2::signal<void(const goby::moos::protobuf::FrontSeatRaw& data)>
        signal_raw_to_frontseat;

    const goby::apps::moos::protobuf::iFrontSeatConfig& cfg() const { return cfg_; }

    void compute_missing(goby::moos::protobuf::CTDSample* ctd_sample);
    void compute_missing(goby::moos::protobuf::NodeStatus* status);

    friend class goby::apps::moos::FrontSeatLegacyTranslator; // to access the signal_state_change
  private:
    void check_error_states();
    void check_change_state();

    // Signals called by FrontSeatInterfaceBase directly. No need to call these
    // from the Frontseat driver implementation
    boost::signals2::signal<void(goby::moos::protobuf::InterfaceState state)> signal_state_change;

    enum Direction
    {
        DIRECTION_TO_FRONTSEAT,
        DIRECTION_FROM_FRONTSEAT
    };

    void glog_raw(const goby::moos::protobuf::FrontSeatRaw& data, Direction direction);

  private:
    const goby::apps::moos::protobuf::iFrontSeatConfig& cfg_;
    goby::moos::protobuf::HelmState helm_state_;
    goby::moos::protobuf::InterfaceState state_;
    goby::time::MicroTime start_time_;
    goby::moos::protobuf::FrontSeatError last_frontseat_error_;
    goby::moos::protobuf::HelmError last_helm_error_;

    CMOOSGeodesy geodesy_;

    std::string glog_out_group_, glog_in_group_;
};
} // namespace moos
} // namespace goby

#endif
