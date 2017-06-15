// Copyright 2009-2017 Toby Schneider (http://gobysoft.org/index.wt/people/toby)
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

#include <boost/assign.hpp>
#include <boost/format.hpp>
#include <boost/math/special_functions/fpclassify.hpp> // for isnan
#include <boost/algorithm/string/replace.hpp>

#include "goby/common/logger.h"
#include "goby/util/as.h"

#include "dccl/binary.h"

#include "bluefin.h"

namespace gpb = goby::moos::protobuf;
using goby::common::goby_time;
using goby::glog;
using goby::util::NMEASentence;
using namespace goby::common::logger;
using namespace goby::common::tcolor;

extern "C"
{
    FrontSeatInterfaceBase* frontseat_driver_load(iFrontSeatConfig* cfg)
    {
        return new BluefinFrontSeat(*cfg);
    }   

}


BluefinFrontSeat::BluefinFrontSeat(const iFrontSeatConfig& cfg)
    : FrontSeatInterfaceBase(cfg),
      bf_config_(cfg.GetExtension(bluefin_config)),
      tcp_(bf_config_.huxley_tcp_address(),
           bf_config_.huxley_tcp_port(), "\r\n",
           bf_config_.reconnect_interval()),
      frontseat_providing_data_(false),
      last_frontseat_data_time_(0),
      frontseat_state_(gpb::FRONTSEAT_NOT_CONNECTED),
      next_connect_attempt_time_(0),
      last_write_time_(0),
      waiting_for_huxley_(false),
      nmea_demerits_(0),
      nmea_present_fail_count_(0),
      last_heartbeat_time_(0)
{
    load_nmea_mappings();
    glog.is(VERBOSE) && glog << "Trying to connect to Huxley server @ " <<bf_config_.huxley_tcp_address() << ":" << bf_config_.huxley_tcp_port() << std::endl;
    tcp_.start();
}

void BluefinFrontSeat::loop()
{
    double now = goby_time<double>();

    // check the connection state
    if(!tcp_.active())
    {
        frontseat_state_ = gpb::FRONTSEAT_NOT_CONNECTED;
    }
    else
    {
        if(frontseat_state_ == gpb::FRONTSEAT_NOT_CONNECTED)
        {
            glog.is(VERBOSE) && glog << "Connected to Huxley." << std::endl;
            frontseat_state_ = gpb::FRONTSEAT_IDLE;
            initialize_huxley();
        }

        check_send_heartbeat();
        try_send();
        try_receive();
    }

    if(now > last_frontseat_data_time_ + bf_config_.allow_missing_nav_interval())
        frontseat_providing_data_ = false;
}

void BluefinFrontSeat::send_command_to_frontseat(const gpb::CommandRequest& command)
{
    if(command.has_cancel_request_id())
    {
        for(std::map<goby::moos::protobuf::BluefinExtraCommands::BluefinCommand, goby::moos::protobuf::CommandRequest>::iterator it = outstanding_requests_.begin(), end = outstanding_requests_.end(); it != end; ++it)
        {
            if(it->second.request_id() == command.cancel_request_id())
            {
                glog.is(DEBUG1) && glog << "Cancelled request: " << it->second.ShortDebugString() << std::endl;
                outstanding_requests_.erase(it);
                return;
            }
        }
        glog.is(DEBUG1) && glog << warn << "Failed to cancel request: " << command.cancel_request_id() << ", could not find such a request." << std::endl;
        return;
    }
    
    gpb::BluefinExtraCommands::BluefinCommand type = gpb::BluefinExtraCommands::UNKNOWN_COMMAND;    

    // extra commands
    if(command.HasExtension(gpb::bluefin_command))
    {
        const gpb::BluefinExtraCommands& bluefin_command = command.GetExtension(gpb::bluefin_command);
        type = bluefin_command.command();
        switch(type)
        {
            case gpb::BluefinExtraCommands::UNKNOWN_COMMAND:
            case gpb::BluefinExtraCommands::DESIRED_COURSE:
                break;                

            case gpb::BluefinExtraCommands::TRIM_ADJUST:
            {
                glog.is(DEBUG1) && glog << "Bluefin Extra Command: Trim adjust requested by backseat."
                                        << std::endl;
                NMEASentence nmea("$BPTRM", NMEASentence::IGNORE);
                nmea.push_back(unix_time2nmea_time(goby_time<double>()));
                append_to_write_queue(nmea);
            }
            break;
                
            case gpb::BluefinExtraCommands::BUOYANCY_ADJUST:
            {
                glog.is(DEBUG1) &&
                    glog << "Bluefin Extra Command: Buoyancy adjustment requested by backseat." << std::endl;
                NMEASentence nmea("$BPBOY", NMEASentence::IGNORE);
                nmea.push_back(unix_time2nmea_time(goby_time<double>()));
                append_to_write_queue(nmea);
            }
            break;
                
            case gpb::BluefinExtraCommands::SILENT_MODE: 
            {
                glog.is(DEBUG1) &&
                    glog << "Bluefin Extra Command: Silent mode change requested by backseat to mode: "
                         << gpb::BluefinExtraCommands::SilentMode_Name(bluefin_command.silent_mode()) << std::endl;
                NMEASentence nmea("$BPSIL", NMEASentence::IGNORE);
                nmea.push_back(unix_time2nmea_time(goby_time<double>()));
                nmea.push_back(static_cast<int>(bluefin_command.silent_mode()));
                append_to_write_queue(nmea);
            }
            break;

            case gpb::BluefinExtraCommands::CANCEL_CURRENT_BEHAVIOR: 
            {
                glog.is(DEBUG1) &&
                    glog << "Bluefin Extra Command: Cancel current behavior requested by backseat." << std::endl;
                NMEASentence nmea("$BPRCE", NMEASentence::IGNORE);
                nmea.push_back(unix_time2nmea_time(goby_time<double>()));
                nmea.push_back(0);
                append_to_write_queue(nmea);
            }
            break;

            case gpb::BluefinExtraCommands::ABORT_MISSION: 
            {
                glog.is(DEBUG1) &&
                    glog << "Bluefin Extra Command: Abort mission requested by backseat; reason: " << gpb::BluefinExtraCommands::AbortReason_Name(bluefin_command.abort_reason()) << std::endl;
                NMEASentence nmea("$BPABT", NMEASentence::IGNORE);
                nmea.push_back(unix_time2nmea_time(goby_time<double>()));
                nmea.push_back("backseat abort");
                nmea.push_back(static_cast<int>(bluefin_command.abort_reason()));
                append_to_write_queue(nmea);
            }
            break;

	    case gpb::BluefinExtraCommands::MISSION_START_CONFIRM: 
            {
                glog.is(DEBUG1) &&
                    glog << "Bluefin Extra Command: Mission start confirmation by backseat " << std::endl;
                NMEASentence nmea("$BPSMC", NMEASentence::IGNORE);
                nmea.push_back(unix_time2nmea_time(goby_time<double>()));
                nmea.push_back(1);//static_cast<int>(bluefin_command.start_confirm())
                append_to_write_queue(nmea);
            }
            break;


	    case gpb::BluefinExtraCommands::MISSION_END_CONFIRM: 
            {
                glog.is(DEBUG1) &&
                    glog << "Bluefin Extra Command: Mission end confirmation by backseat " << std::endl;
                NMEASentence nmea("$BPRCE", NMEASentence::IGNORE);
                nmea.push_back(unix_time2nmea_time(goby_time<double>()));
                nmea.push_back(0);
                append_to_write_queue(nmea);
            }
            break;
        }
    }
    
    if(command.has_desired_course())
    {
        if(type != gpb::BluefinExtraCommands::UNKNOWN_COMMAND &&
           type != gpb::BluefinExtraCommands::DESIRED_COURSE)
        {
            glog.is(WARN) && glog << "Ignoring desired course information in this message, as an extra command was set. Only one command allowed per message." << std::endl;
        }
        else if(static_cast<int>(command.desired_course().depth()) == 0 &&
                static_cast<int>(command.desired_course().speed()) == 0)
        {
            type = gpb::BluefinExtraCommands::DESIRED_COURSE;   
            NMEASentence nmea("$BPRMB", NMEASentence::IGNORE);
            nmea.push_back(unix_time2nmea_time(goby_time<double>()));
            nmea.push_back(0); // zero rudder
            nmea.push_back(0); // zero pitch
            nmea.push_back(2); // previous field is a pitch [2]
            nmea.push_back(0); // zero rpm
            nmea.push_back(0); // previous field is an rpm [0]
            nmea.push_back(1); // first field is a rudder adjustment [1]
            append_to_write_queue(nmea);
        }
        else
        {
            type = gpb::BluefinExtraCommands::DESIRED_COURSE;   
            NMEASentence nmea("$BPRMB", NMEASentence::IGNORE);
            nmea.push_back(unix_time2nmea_time(goby_time<double>()));
            const goby::moos::protobuf::DesiredCourse& desired_course = command.desired_course();


            // for zero speed, send zero RPM, pitch, rudder
            if(desired_course.speed() < 0.01)
            {
                nmea.push_back(0); // zero rudder
                nmea.push_back(0); // zero pitch
                nmea.push_back(2); // previous field is a pitch [2]
                nmea.push_back(0); // zero rpm
                nmea.push_back(0); // previous field is an rpm [0]
                nmea.push_back(1); // first field is a rudder adjustment [1]
            }
            else
            {
                nmea.push_back(desired_course.heading());
                nmea.push_back(desired_course.depth());
                nmea.push_back(0); // previous field is a depth (not altitude [1] or pitch [2])
                nmea.push_back(desired_course.speed());
                nmea.push_back(1); // previous field is a speed (not rpm [0])
                nmea.push_back(0); // first field is a heading (not rudder adjustment [1])
            }
            
            append_to_write_queue(nmea);
        }
    }

    if(!bf_config_.disable_ack() && command.response_requested())
    {
        if(outstanding_requests_.count(type))
        {
            glog.is(WARN) && glog << "Request already outstanding for type: " << gpb::BluefinExtraCommands::BluefinCommand_Name(type)  << ", overwriting old request." << std::endl;
        }

        outstanding_requests_[type] = command;
    }
    

}
    
void BluefinFrontSeat::send_data_to_frontseat(const gpb::FrontSeatInterfaceData& data)
{
    //    glog.is(DEBUG1) && glog << "Data to FS: " << data.DebugString() << std::endl;

    // we need to send our CTD to Bluefin when we have it attached to our payload
    if(data.has_ctd_sample())
    {
        NMEASentence nmea("$BPCTD", NMEASentence::IGNORE);
        nmea.push_back(unix_time2nmea_time(goby_time<double>()));

        // BF wants Siemens / meter, same as CTDSample
        if(data.ctd_sample().has_conductivity())
            nmea.push_back(goby::util::as<std::string>(data.ctd_sample().conductivity(), 10));
        else
            nmea.push_back("");

        // degrees C
        if(!(boost::math::isnan)(data.ctd_sample().temperature()))
            nmea.push_back(goby::util::as<std::string>(data.ctd_sample().temperature(), 10));
        else
            nmea.push_back("");
            
        // BF wants kPA; CTD sample uses Pascals
        if(!(boost::math::isnan)(data.ctd_sample().pressure()))
            nmea.push_back(goby::util::as<std::string>(data.ctd_sample().pressure()/1.0e3, 10));
        else
            nmea.push_back("");
            
        nmea.push_back(unix_time2nmea_time(data.ctd_sample().time()));
        
        append_to_write_queue(nmea);   
    }

    if(data.has_dccl_message())
    {
        NMEASentence nmea("$BPDCL", NMEASentence::IGNORE);
        nmea.push_back(unix_time2nmea_time(goby_time<double>()));
        nmea.push_back(boost::trim_copy(dccl::b64_encode(data.dccl_message())));
        append_to_write_queue(nmea);
    }
    
    if(data.HasExtension(gpb::bluefin_data))
    {
        const gpb::BluefinExtraData& bf_extra = data.GetExtension(gpb::bluefin_data);
        // Bluefin wants our MicroModem feed so they can feel warm and fuzzy ... ?
        if(bf_extra.has_micro_modem_raw_in() && bf_config_.send_tmr_messages())
        {
            NMEASentence nmea("$BPTMR", NMEASentence::IGNORE);        
            nmea.push_back(unix_time2nmea_time(goby_time<double>()));
            const int TRANSPORT_ACOUSTIC_MODEM = 3;
            nmea.push_back(TRANSPORT_ACOUSTIC_MODEM);

            std::string modem_nmea = bf_extra.micro_modem_raw_in().raw();
            boost::replace_all(modem_nmea, ",", ":");
            boost::replace_all(modem_nmea, "*", "/");
            boost::replace_all(modem_nmea, "\r", " ");
            nmea.push_back(modem_nmea);
            append_to_write_queue(nmea);
        }

        for(int i = 0, n = bf_extra.payload_status_size(); i < n; ++i)
            payload_status_.insert(std::make_pair(bf_extra.payload_status(i).expire_time(), bf_extra.payload_status(i)));
    }
    
}

void BluefinFrontSeat::send_raw_to_frontseat(const gpb::FrontSeatRaw& data)
{
    try
    {    
        NMEASentence nmea(data.raw(),NMEASentence::IGNORE);
        append_to_write_queue(nmea);
    }
    catch(goby::util::bad_nmea_sentence& e)
    {
        glog.is(DEBUG1) && glog << warn << "Refusing to send this invalid message: " << data.raw() << ", " << e.what() << std::endl;
    }
}

void BluefinFrontSeat::check_send_heartbeat()
{
    double now = goby_time<double>();
    if(now > last_heartbeat_time_ + bf_config_.heartbeat_interval())
    {
        NMEASentence nmea("$BPSTS",NMEASentence::IGNORE);
        
        nmea.push_back(unix_time2nmea_time(goby_time<double>()));
        const int FAILED = 0, ALL_OK = 1;
        bool ok = state() != gpb::INTERFACE_HELM_ERROR && state() != gpb::INTERFACE_FS_ERROR;

        std::string status;
        if(payload_status_.size())
        {
            std::map<int, std::string> seen_ids;            
            status += goby::common::goby_time_as_string();
            payload_status_.erase(payload_status_.begin(), payload_status_.upper_bound(goby::common::goby_time<goby::uint64>()));

            for(std::multimap<goby::uint64, goby::moos::protobuf::BluefinExtraData::PayloadStatus>::const_iterator it = payload_status_.begin(), end = payload_status_.end(); it != end; ++it)
            {
                // only display the newest from a given ID
                if(!seen_ids.count(it->second.id()))
                {
                    ok = ok && it->second.all_ok();
                    seen_ids[it->second.id()] = it->second.msg();
                }
            }
            
            for(std::map<int, std::string>::const_iterator it = seen_ids.begin(), end = seen_ids.end(); it != end; ++it)
            {
                status += "; ";
                status += it->second;
            }
        }
        
        if(status.empty())
            status = "Deploy";
        
        nmea.push_back(ok ? ALL_OK : FAILED);
        nmea.push_back(status);
        append_to_write_queue(nmea);

        last_heartbeat_time_ = now + bf_config_.heartbeat_interval();
    }
}

void BluefinFrontSeat::try_receive()
{
    std::string in;
    while(tcp_.readline(&in))
    {
        boost::trim(in);
        // try to handle the received message, posting appropriate signals
        try
        {
            NMEASentence nmea(in, NMEASentence::VALIDATE);
            process_receive(nmea);
        }
        catch(std::exception& e)
        {
            glog.is(DEBUG1) && glog << warn << "Failed to handle message: " << e.what() << std::endl;
        }            
    }
}

void BluefinFrontSeat::initialize_huxley()
{
    nmea_demerits_ = 0;
    waiting_for_huxley_ = false;
    out_.clear();
    pending_.clear();
    
    using boost::assign::operator+=;
    std::vector<SentenceIDs> log_requests;
    if(!bf_config_.disable_ack())
        log_requests += ACK; // must request ACK first so we get NMEA ACKs for the other messages
    log_requests += NVG,MIS,MSC,NVR,SVS,RVL,SHT,TOP,MBS,MBE,CTD,DVL,BOY,TRM;
    
    if(bf_config_.accepting_commands_hook() == BluefinFrontSeatConfig::BFCTL_TRIGGER)
        log_requests += CTL;
     

    NMEASentence nmea("$BPLOG", NMEASentence::IGNORE);
    nmea.push_back("");
    nmea.push_back("ON");
    for(std::vector<SentenceIDs>::const_iterator it = log_requests.begin(),
            end = log_requests.end(); it != end; ++it)
    {
        nmea[1] = sentence_id_map_.right.at(*it);
        append_to_write_queue(nmea);
    }

    for(int i = 0, n = bf_config_.extra_bplog_size(); i < n; ++i)
    {
        nmea[1] = boost::to_upper_copy(bf_config_.extra_bplog(i));
        append_to_write_queue(nmea);
    }
}

void BluefinFrontSeat::append_to_write_queue(const NMEASentence& nmea)
{
    out_.push_back(nmea);
    try_send(); // try to push it now without waiting for the next call to do_work();
}


void BluefinFrontSeat::try_send()
{
    if(out_.empty()) return;

    const NMEASentence& nmea = out_.front();
    
    bool resend = waiting_for_huxley_ &&
        (last_write_time_ <= (goby_time<double>() - bf_config_.nmea_resend_interval()));
    
    if(!waiting_for_huxley_)
    {
        write(nmea);
    }
    else if(resend)
    {
        glog.is(DEBUG1) && glog << "resending last command; no NMEA ack in "
                                << bf_config_.nmea_resend_interval() << " second(s). "
                                << std::endl;
        
        try
        {
            // try to increment the present (current NMEA sentence) fail counter
            // will throw if fail counter exceeds nmea_resend_attempts
            ++nmea_present_fail_count_;
            if(nmea_present_fail_count_ >= bf_config_.nmea_resend_attempts())
                throw(FrontSeatException(gpb::ERROR_FRONTSEAT_IGNORING_COMMANDS));
            // assuming we're still ok, write the line again
            write(nmea);
        }
        catch(FrontSeatException& e)
        {
            glog.is(DEBUG1) && glog << "Huxley did not respond to our command even after "
                                    << bf_config_.nmea_resend_attempts()
                                    << " retries. continuing onwards anyway..."
                                    << std::endl;
            remove_from_write_queue();
            
            ++nmea_demerits_;
            if(nmea_demerits_ > bf_config_.allowed_nmea_demerits())
            {
                glog.is(WARN) && glog << "Huxley server is connected but appears to not be responding." << std::endl;
                // force a disconnect
                frontseat_state_ = gpb::FRONTSEAT_NOT_CONNECTED;
                throw;
            }
        }
    }
}


void BluefinFrontSeat::remove_from_write_queue()
{
    waiting_for_huxley_ = false;

    if(!out_.empty())
        out_.pop_front();
    else
    {
        glog.is(DEBUG1) && glog << "Expected to pop outgoing NMEA message but out_ deque is empty" << std::endl;
    }
    
    nmea_present_fail_count_ = 0;
}


void BluefinFrontSeat::write(const NMEASentence& nmea)
{
    gpb::FrontSeatRaw raw_msg;
    raw_msg.set_raw(nmea.message());
    raw_msg.set_description(description_map_[nmea.front()]);

    signal_raw_to_frontseat(raw_msg);
    
    tcp_.write(nmea.message_cr_nl());

    if(bf_config_.disable_ack())
    {
        remove_from_write_queue();
    }
    else
    {
        waiting_for_huxley_ = true;
        last_write_time_ = goby_time<double>();
    }
}


void BluefinFrontSeat::process_receive(const NMEASentence& nmea)
{
    gpb::FrontSeatRaw raw_msg;
    raw_msg.set_raw(nmea.message());
    raw_msg.set_description(description_map_[nmea.front()]);
    
    signal_raw_from_frontseat(raw_msg);
    
    nmea_demerits_ = 0;
    
    // look at the sentence id (last three characters of the NMEA 0183 talker)
    switch(sentence_id_map_.left.at(nmea.sentence_id()))
    {
        case ACK: bfack(nmea); break; // nmea ack

        case NVG: bfnvg(nmea); break; // navigation
        case NVR: bfnvr(nmea); break; // velocity and rate
        case RVL: bfrvl(nmea); break; // raw vehicle speed

        case DVL: bfdvl(nmea); break; // raw DVL data
        case CTD: bfctd(nmea); break; // raw CTD sensor data
        case SVS: bfsvs(nmea); break; // sound velocity
            
        case MSC: bfmsc(nmea); break; // payload mission command
        case SHT: bfsht(nmea); break; // payload shutdown

        case MBS: bfmbs(nmea); break; // begin new behavior
        case MIS: bfmis(nmea); break; // mission status
        case MBE: bfmbe(nmea); break; // end behavior

        case CTL: bfctl(nmea); break; // backseat control message (SPI 1.10+)
            

            
        case BOY: bfboy(nmea); break; // buoyancy status
        case TRM: bftrm(nmea); break; // trim status

        case TOP: bftop(nmea); break; // request to send data topside   
        default: break;
    }    
}

std::string BluefinFrontSeat::unix_time2nmea_time(double time)
{
    boost::posix_time::ptime ptime = goby::common::unix_double2ptime(time);
    
    // HHMMSS.SSS
    // it appears that exactly three digits of precision is important (sometimes)
    boost::format f("%02d%02d%02d.%03d");
    f % ptime.time_of_day().hours() %
        ptime.time_of_day().minutes() %
        ptime.time_of_day().seconds() %
        (ptime.time_of_day().fractional_seconds() * 1000 /
         boost::posix_time::time_duration::ticks_per_second());
    
    return f.str();
}



void BluefinFrontSeat::load_nmea_mappings()
{
    boost::assign::insert (sentence_id_map_)
        ("MSC",MSC)("SHT",SHT)("BDL",BDL)("SDL",SDL)
        ("TOP",TOP)("DVT",DVT)("VER",VER)("NVG",NVG)
        ("SVS",SVS)("RCM",RCM)("RDP",RDP)("RVL",RVL)
        ("RBS",RBS)("MBS",MBS)("MBE",MBE)("MIS",MIS)
        ("ERC",ERC)("DVL",DVL)("DV2",DV2)("IMU",IMU)
        ("CTD",CTD)("RNV",RNV)("PIT",PIT)("CNV",CNV)
        ("PLN",PLN)("ACK",ACK)("TRM",TRM)("LOG",LOG)
        ("STS",STS)("DVR",DVR)("CPS",CPS)("CPR",CPR)
        ("TRK",TRK)("RTC",RTC)("RGP",RGP)("RCN",RCN)
        ("RCA",RCA)("RCB",RCB)("RMB",RMB)("EMB",EMB)
        ("TMR",TMR)("ABT",ABT)("KIL",KIL)("MSG",MSG)
        ("RMP",RMP)("SEM",SEM)("NPU",NPU)("CPD",CPD)
        ("SIL",SIL)("BOY",BOY)("SUS",SUS)("CON",CON)
        ("RES",RES)("SPD",SPD)("SAN",SAN)("GHP",GHP)
        ("GBP",GBP)("RNS",RNS)("RBO",RBO)("CMA",CMA)
        ("NVR",NVR)("TEL",TEL)("CTL",CTL)("DCL",DCL);
    
    boost::assign::insert (talker_id_map_)
        ("BF",BF)("BP",BP);

    boost::assign::insert (description_map_)
        ("$BFMSC","Payload Mission Command")
        ("$BFSHT","Payload Shutdown")
        ("$BFBDL","Begin Data Logging")

        ("$BFSDL","Stop Data Logging")
        ("$BFTOP","Topside Message (Not Implemented) ")
        ("$BFDVT","Begin/End DVL External Triggering")
        ("$BFVER","Vehicle Interface Version")
        ("$BFNVG","Navigation Update")
        ("$BFNVR","Velocity and Rate Update")
        ("$BFTEL","Telemetry Status (Not Implemented)")
        ("$BFSVS","Sound Velocity")
        ("$BFRCM","Raw Compass Data")
        ("$BFRDP","Raw Depth Sensor Data")
        ("$BFRVL","Raw Vehicle Speed")
        ("$BFRBS","Battery Voltage")
        ("$BFMBS","Begin New Behavior")
        ("$BFMBE","End Behavior")
        ("$BFMIS","Mission Status")
        ("$BFERC","Elevator and Rudder Data")
        ("$BFDVL","Raw DVL Data")
        ("$BFDV2","Raw DVL Data, Extended")
        ("$BFIMU","Raw IMU Data")
        ("$BFCTD","Raw CTD Sensor Data")
        ("$BFRNV","Relative Navigation Position")
        ("$BFPIT","Pitch Servo Positions")
        ("$BFCNV","Cartesian Relative Navigation Position")
        ("$BFPLN","Mission Plan Element")
        ("$BFACK","Message Acknowledgement")
        ("$BFTRM","Trim Status")
	("$BPSMC","Confirm Mission Start")
        ("$BFBOY","Buoyancy Status")
        ("$BPLOG","Logging Control")
        ("$BPSTS","Payload Status Message")
        ("$BPTOP","Request to Send Data Topside")
        ("$BPDVR","Request to Change DVL Triggering Method")
        ("$BPTRK","Request Additional Trackline")
        ("$BPRTC","Request Additional Trackcircle")
        ("$BPRGP","Request Additional GPS Hits")
        ("$BPRCN","Cancel Requested Behavior")
        ("$BPRCE","Cancel Current Mission Element")
        ("$BPRCA","Cancel All Requested Behaviors")
        ("$BPRCB","Cancel Current Behavior")
        ("$BPRMB","Modify Current Behavior")
        ("$BPEMB","End Behavior Modify")
        ("$BPTMR","Topside Message Relay (Not Available on Most Vehicles)")
        ("$BPCTD","Raw CTD Sensor Data")
        ("$BPABT","Abort Mission")
        ("$BPKIL","Kill Mission")
        ("$BPMSG","Log Message")
        ("$BPRMP","Request Mission Plan")
        ("$BPSEM","Start Empty Mission (Not Implemented)")
        ("$BPNPU","Navigation Position Update")
        ("$BPSIL","Silent Mode")
        ("$BPTRM","Request Trim Adjustment Behavior")
        ("$BPBOY","Request Buoyancy Adjustment Behavior")
        ("$BPVER","Payload Interface Version")
        ("$BPSUS","Suspend Mission")
        ("$BPCON","Continue")
        ("$BPRES","Resume Mission")
        ("$BPSPD","Hull Relative Speed Limit")
        ("$BPSAN","Set Sonar Angle")
        ("$BPGHP","Go To Hull Position")
        ("$BPGBP","Go to Bottom Position")
        ("$BPRNS","Reset Relative Navigation")
        ("$BPRBO","Hull Relative Bearing Offset")
        ("$BFCMA","Communications Medium Access")
        ("$BFCPS","Communications Packet Sent")
        ("$BFCPR","Communications Packet Received Data")
        ("$BPCPD","Communications Packet Data")
        ("$BFCTL","Backseat Control")
        ("$BPDCL","Forward DCCL message to Huxley from Payload"); 

}
