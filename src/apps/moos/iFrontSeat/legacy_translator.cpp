// Copyright 2013-2019:
//   GobySoft, LLC (2013-)
//   Massachusetts Institute of Technology (2007-2014)
//   Community contributors (see AUTHORS file)
// File authors:
//   Toby Schneider <toby@gobysoft.org>
//
//
// This file is part of the Goby Underwater Autonomy Project Binaries
// ("The Goby Binaries").
//
// The Goby Binaries are free software: you can redistribute them and/or modify
// them under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 2 of the License, or
// (at your option) any later version.
//
// The Goby Binaries are distributed in the hope that they will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with Goby.  If not, see <http://www.gnu.org/licenses/>.

#include "goby/acomms/connect.h"
#include "goby/moos/frontseat/bluefin/bluefin.pb.h"

#include "iFrontSeat.h"
#include "legacy_translator.h"

namespace gpb = goby::moos::protobuf;
using goby::glog;
using namespace goby::util::logger;

goby::apps::moos::FrontSeatLegacyTranslator::FrontSeatLegacyTranslator(iFrontSeat* fs)
    : ifs_(fs), request_id_(0)
{
    if (ifs_->cfg_.legacy_cfg().subscribe_ctd())
    {
        std::vector<std::string> ctd_params(
            {"CONDUCTIVITY", "TEMPERATURE", "PRESSURE", "SALINITY"});

        for (std::vector<std::string>::const_iterator it = ctd_params.begin(),
                                                      end = ctd_params.end();
             it != end; ++it)
        { ifs_->subscribe("CTD_" + *it, &FrontSeatLegacyTranslator::handle_mail_ctd, this, 1); }

        ctd_sample_.set_temperature(std::numeric_limits<double>::quiet_NaN());
        ctd_sample_.set_pressure(std::numeric_limits<double>::quiet_NaN());
        ctd_sample_.set_salinity(std::numeric_limits<double>::quiet_NaN());
        ctd_sample_.set_lat(std::numeric_limits<double>::quiet_NaN());
        ctd_sample_.set_lon(std::numeric_limits<double>::quiet_NaN());
        // we'll let FrontSeatInterfaceBase::compute_missing() give us density, sound speed & depth
    }

    if (ifs_->cfg_.legacy_cfg().subscribe_desired())
    {
        std::vector<std::string> desired_params(
            {"HEADING", "SPEED", "DEPTH", "PITCH", "ROLL", "Z_RATE", "ALTITUDE"});

        for (std::vector<std::string>::const_iterator it = desired_params.begin(),
                                                      end = desired_params.end();
             it != end; ++it)
        {
            ifs_->subscribe("DESIRED_" + *it,
                            &FrontSeatLegacyTranslator::handle_mail_desired_course, this, 1);
        }
    }

    if (ifs_->cfg_.legacy_cfg().subscribe_acomms_raw())
    {
        ifs_->subscribe(
            "ACOMMS_RAW_INCOMING",
            boost::bind(&FrontSeatLegacyTranslator::handle_mail_modem_raw, this, _1, INCOMING));
        ifs_->subscribe(
            "ACOMMS_RAW_OUTGOING",
            boost::bind(&FrontSeatLegacyTranslator::handle_mail_modem_raw, this, _1, OUTGOING));
    }

    if (ifs_->cfg_.legacy_cfg().pub_sub_bf_commands())
    {
        ifs_->subscribe("BUOYANCY_CONTROL",
                        &FrontSeatLegacyTranslator::handle_mail_buoyancy_control, this);
        ifs_->subscribe("TRIM_CONTROL", &FrontSeatLegacyTranslator::handle_mail_trim_control, this);
        ifs_->subscribe("FRONTSEAT_BHVOFF",
                        &FrontSeatLegacyTranslator::handle_mail_frontseat_bhvoff, this);
        ifs_->subscribe("FRONTSEAT_SILENT",
                        &FrontSeatLegacyTranslator::handle_mail_frontseat_silent, this);
        ifs_->subscribe("BACKSEAT_ABORT", &FrontSeatLegacyTranslator::handle_mail_backseat_abort,
                        this);
    }

    goby::acomms::connect(&ifs_->frontseat_->signal_data_from_frontseat, this,
                          &FrontSeatLegacyTranslator::handle_driver_data_from_frontseat);

    if (ifs_->cfg_.legacy_cfg().publish_fs_bs_ready())
    {
        goby::acomms::connect(&ifs_->frontseat_->signal_state_change, this,
                              &FrontSeatLegacyTranslator::set_fs_bs_ready_flags);
    }
}
void goby::apps::moos::FrontSeatLegacyTranslator::handle_driver_data_from_frontseat(
    const goby::moos::protobuf::FrontSeatInterfaceData& data)
{
    if (data.has_node_status() && ifs_->cfg_.legacy_cfg().publish_nav())
    {
        const goby::moos::protobuf::NodeStatus& status = data.node_status();

        ctd_sample_.set_lat(status.global_fix().lat());
        ctd_sample_.set_lon(status.global_fix().lon());

        // post NAV_*
        ifs_->publish("NAV_X", status.local_fix().x());
        ifs_->publish("NAV_Y", status.local_fix().y());
        ifs_->publish("NAV_LAT", status.global_fix().lat());
        ifs_->publish("NAV_LONG", status.global_fix().lon());

        if (status.local_fix().has_z())
            ifs_->publish("NAV_Z", status.local_fix().z());
        if (status.global_fix().has_depth())
            ifs_->publish("NAV_DEPTH", status.global_fix().depth());

        const double pi = 3.14159;
        if (status.pose().has_heading())
        {
            double yaw = status.pose().heading() * pi / 180.0;
            while (yaw < -pi) yaw += 2 * pi;
            while (yaw >= pi) yaw -= 2 * pi;
            ifs_->publish("NAV_YAW", yaw);
            ifs_->publish("NAV_HEADING", status.pose().heading());
        }

        ifs_->publish("NAV_SPEED", status.speed());

        if (status.pose().has_pitch())
            ifs_->publish("NAV_PITCH", status.pose().pitch() * pi / 180.0);
        if (status.pose().has_roll())
            ifs_->publish("NAV_ROLL", status.pose().roll() * pi / 180.0);

        if (status.global_fix().has_altitude())
            ifs_->publish("NAV_ALTITUDE", status.global_fix().altitude());

        // surface for GPS variable
        if (status.global_fix().lat_source() == goby::moos::protobuf::GPS &&
            status.global_fix().lon_source() == goby::moos::protobuf::GPS)
        {
            std::stringstream ss;
            ss << "Timestamp=" << std::setprecision(15) << status.time();
            ifs_->publish("GPS_UPDATE_RECEIVED", ss.str());
        }
    }

    if (data.HasExtension(gpb::bluefin_data) && ifs_->cfg_.legacy_cfg().pub_sub_bf_commands())
    {
        const gpb::BluefinExtraData& bf_data = data.GetExtension(gpb::bluefin_data);
        if (bf_data.has_trim_status())
        {
            std::stringstream trim_report;
            const gpb::TrimStatus& trim = bf_data.trim_status();

            trim_report << "status=" << static_cast<int>(trim.status())
                        << ",error=" << static_cast<int>(trim.error())
                        << ",trim_pitch=" << trim.pitch_trim_degrees()
                        << ",trim_roll=" << trim.roll_trim_degrees();

            ifs_->publish("TRIM_REPORT", trim_report.str());
        }

        if (bf_data.has_buoyancy_status())
        {
            std::stringstream buoyancy_report;
            const gpb::BuoyancyStatus& buoyancy = bf_data.buoyancy_status();
            buoyancy_report << "status=" << static_cast<int>(buoyancy.status())
                            << ",error=" << static_cast<int>(buoyancy.error())
                            << ",buoyancy=" << buoyancy.buoyancy_newtons();
            ifs_->publish("BUOYANCY_REPORT", buoyancy_report.str());
        }
    }
}

void goby::apps::moos::FrontSeatLegacyTranslator::handle_mail_ctd(const CMOOSMsg& msg)
{
    const std::string& key = msg.GetKey();
    if (key == "CTD_CONDUCTIVITY")
    {
        // - should be in siemens/meter, assuming it's a SeaBird 49 SBE
        // using iCTD. Thus, no conversion needed (see ctd_sample.proto)
        // - we need to clean up this units conversion

        ctd_sample_.set_conductivity(msg.GetDouble());
    }
    else if (key == "CTD_TEMPERATURE")
    {
        // - degrees C is a safe assumption
        ctd_sample_.set_temperature(msg.GetDouble());

        // We'll use the variable to key postings, since it's
        // always present (even in simulations)
        ctd_sample_.set_time(msg.GetTime());
        gpb::FrontSeatInterfaceData data;
        *data.mutable_ctd_sample() = ctd_sample_;
        ifs_->frontseat_->compute_missing(data.mutable_ctd_sample());

        ifs_->publish_pb(ifs_->cfg_.moos_var().prefix() + ifs_->cfg_.moos_var().data_to_frontseat(),
                         data);
    }
    else if (key == "CTD_PRESSURE")
    {
        // - MOOS var is decibars assuming it's a SeaBird 49 SBE using iCTD.
        // - GLINT10 data supports this assumption
        // - CTDSample uses Pascals

        const double dBar_TO_Pascal = 1e4; // 1 dBar == 10000 Pascals
        ctd_sample_.set_pressure(msg.GetDouble() * dBar_TO_Pascal);
    }
    else if (key == "CTD_SALINITY")
    {
        // salinity is standardized to practical salinity scale
        ctd_sample_.set_salinity(msg.GetDouble());
    }
}

void goby::apps::moos::FrontSeatLegacyTranslator::handle_mail_desired_course(const CMOOSMsg& msg)
{
    const std::string& key = msg.GetKey();
    if (key == "DESIRED_SPEED")
    {
        desired_course_.set_speed(msg.GetDouble());
        desired_course_.set_time(msg.GetTime());
        gpb::CommandRequest command;
        *command.mutable_desired_course() = desired_course_;
        command.set_response_requested(true);
        command.set_request_id(LEGACY_REQUEST_IDENTIFIER + request_id_++);

        ifs_->publish_pb(ifs_->cfg_.moos_var().prefix() + ifs_->cfg_.moos_var().command_request(),
                         command);
    }
    else if (key == "DESIRED_HEADING")
    {
        desired_course_.set_heading(msg.GetDouble());
    }
    else if (key == "DESIRED_DEPTH")
    {
        desired_course_.set_depth(msg.GetDouble());
    }
    else if (key == "DESIRED_PITCH")
    {
        desired_course_.set_pitch(msg.GetDouble());
    }
    else if (key == "DESIRED_ROLL")
    {
        desired_course_.set_roll(msg.GetDouble());
    }
    else if (key == "DESIRED_Z_RATE")
    {
        desired_course_.set_z_rate(msg.GetDouble());
    }
    else if (key == "DESIRED_ALTITUDE")
    {
        desired_course_.set_altitude(msg.GetDouble());
    }
}

void goby::apps::moos::FrontSeatLegacyTranslator::handle_mail_modem_raw(const CMOOSMsg& msg,
                                                                        ModemRawDirection direction)
{
    goby::acomms::protobuf::ModemRaw raw;
    parse_for_moos(msg.GetString(), &raw);
    gpb::FrontSeatInterfaceData data;

    switch (direction)
    {
        case OUTGOING:
            *data.MutableExtension(gpb::bluefin_data)->mutable_micro_modem_raw_out() = raw;
            break;
        case INCOMING:
            *data.MutableExtension(gpb::bluefin_data)->mutable_micro_modem_raw_in() = raw;
            break;
    }

    ifs_->publish_pb(ifs_->cfg_.moos_var().prefix() + ifs_->cfg_.moos_var().data_to_frontseat(),
                     data);
}

void goby::apps::moos::FrontSeatLegacyTranslator::set_fs_bs_ready_flags(
    goby::moos::protobuf::InterfaceState state)
{
    goby::moos::protobuf::FrontSeatInterfaceStatus status = ifs_->frontseat_->status();
    if (status.frontseat_state() == gpb::FRONTSEAT_ACCEPTING_COMMANDS)
        ifs_->publish("FRONTSEAT_READY", 1);
    else
        ifs_->publish("FRONTSEAT_READY", 0);

    if (status.helm_state() == gpb::HELM_DRIVE)
        ifs_->publish("BACKSEAT_READY", 1);
    else
        ifs_->publish("BACKSEAT_READY", 0);
}

void goby::apps::moos::FrontSeatLegacyTranslator::handle_mail_buoyancy_control(const CMOOSMsg& msg)
{
    if (goby::util::as<bool>(boost::trim_copy(msg.GetString())))
    {
        gpb::CommandRequest command;
        command.set_response_requested(true);
        command.set_request_id(LEGACY_REQUEST_IDENTIFIER + request_id_++);
        gpb::BluefinExtraCommands* bluefin_command = command.MutableExtension(gpb::bluefin_command);
        bluefin_command->set_command(gpb::BluefinExtraCommands::BUOYANCY_ADJUST);

        publish_command(command);
    }
}

void goby::apps::moos::FrontSeatLegacyTranslator::handle_mail_trim_control(const CMOOSMsg& msg)
{
    if (goby::util::as<bool>(boost::trim_copy(msg.GetString())))
    {
        gpb::CommandRequest command;
        command.set_response_requested(true);
        command.set_request_id(LEGACY_REQUEST_IDENTIFIER + request_id_++);
        gpb::BluefinExtraCommands* bluefin_command = command.MutableExtension(gpb::bluefin_command);
        bluefin_command->set_command(gpb::BluefinExtraCommands::TRIM_ADJUST);

        publish_command(command);
    }
}

void goby::apps::moos::FrontSeatLegacyTranslator::handle_mail_frontseat_bhvoff(const CMOOSMsg& msg)
{
    if (goby::util::as<bool>(boost::trim_copy(msg.GetString())))
    {
        gpb::CommandRequest command;
        command.set_response_requested(true);
        command.set_request_id(LEGACY_REQUEST_IDENTIFIER + request_id_++);
        gpb::BluefinExtraCommands* bluefin_command = command.MutableExtension(gpb::bluefin_command);
        bluefin_command->set_command(gpb::BluefinExtraCommands::CANCEL_CURRENT_BEHAVIOR);

        publish_command(command);
    }
}

void goby::apps::moos::FrontSeatLegacyTranslator::handle_mail_frontseat_silent(const CMOOSMsg& msg)
{
    gpb::CommandRequest command;
    command.set_response_requested(true);
    command.set_request_id(LEGACY_REQUEST_IDENTIFIER + request_id_++);
    gpb::BluefinExtraCommands* bluefin_command = command.MutableExtension(gpb::bluefin_command);
    bluefin_command->set_command(gpb::BluefinExtraCommands::SILENT_MODE);

    if (goby::util::as<bool>(boost::trim_copy(msg.GetString())))
        bluefin_command->set_silent_mode(gpb::BluefinExtraCommands::SILENT);
    else
        bluefin_command->set_silent_mode(gpb::BluefinExtraCommands::NORMAL);

    publish_command(command);
}

void goby::apps::moos::FrontSeatLegacyTranslator::handle_mail_backseat_abort(const CMOOSMsg& msg)
{
    gpb::CommandRequest command;
    command.set_response_requested(true);
    command.set_request_id(LEGACY_REQUEST_IDENTIFIER + request_id_++);
    gpb::BluefinExtraCommands* bluefin_command = command.MutableExtension(gpb::bluefin_command);
    bluefin_command->set_command(gpb::BluefinExtraCommands::ABORT_MISSION);

    if (goby::util::as<int>(msg.GetDouble()) == 0)
        bluefin_command->set_abort_reason(gpb::BluefinExtraCommands::SUCCESSFUL_MISSION);
    else
        bluefin_command->set_abort_reason(gpb::BluefinExtraCommands::ABORT_WITH_ERRORS);

    publish_command(command);
}

void goby::apps::moos::FrontSeatLegacyTranslator::publish_command(
    const gpb::CommandRequest& command)
{
    ifs_->publish_pb(ifs_->cfg_.moos_var().prefix() + ifs_->cfg_.moos_var().command_request(),
                     command);
}
