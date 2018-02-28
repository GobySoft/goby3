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

#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/units/base_units/metric/knot.hpp>
#include <boost/units/systems/si/prefixes.hpp>


#include "goby/common/logger.h"
#include "goby/common/time.h"
#include "goby/util/binary.h"

#include "rudics_packet.h"
#include "benthos_atm900_driver_fsm.h"
#include "benthos_atm900_driver.h"

using goby::glog;
using namespace goby::common::logger;
using goby::common::goby_time;

int goby::acomms::benthos_fsm::BenthosATM900FSM::count_ = 0;

void goby::acomms::benthos_fsm::BenthosATM900FSM::buffer_data_out( const goby::acomms::protobuf::ModemTransmission& msg)
{
    data_out_.push_back(msg);
}




void goby::acomms::benthos_fsm::Active::in_state_react(const EvRxSerial& e)
{
    std::string in = e.line;
    boost::trim(in);

    static const std::string data = "DATA";
    static const std::string user = "user:";

    if(in.compare(0, data.size(), data) == 0)
    {
        // data
        post_event(EvReceive(in));
    }
    else if(in.compare(0, user.size(), user) == 0)  // shell prompt
    {
        post_event(EvShellPrompt());

        if(in.find("Lowpower") != std::string::npos)
            post_event(EvAck(in));

    }
    else     // presumably a response to something else
    {
        post_event(EvAck(in));
        
        static const std::string connect = "CONNECT";
        if(in.compare(0, connect.size(), connect) == 0)
            post_event(EvConnect());
    }
}

goby::acomms::benthos_fsm::ReceiveData::ReceiveData(my_context ctx) :
    my_base(ctx), StateNotify("ReceiveData"), reported_size_(0)
{
    try 
    {
        if(const EvReceive* ev_rx = dynamic_cast<const EvReceive*>(triggering_event()))
        {
            std::string first = ev_rx->first_;
            // remove extra spaces in the string
            boost::erase_all(first, " ");
            // e.g. DATA(0037):b2b645b7097cb585d181b0c34ff1a13b
            enum { SIZE_START = 5, SIZE_END = 9, BYTES_START = 11 };
            if(first.size() < BYTES_START)
                throw(std::runtime_error("String too short"));

            std::string size_str = first.substr(SIZE_START, SIZE_END-SIZE_START);
            boost::trim_left_if(size_str, boost::is_any_of("0"));
            reported_size_ = boost::lexical_cast<unsigned>(size_str);
            encoded_bytes_ += goby::util::hex_decode(first.substr(BYTES_START));
        }
        else
        {
            throw(std::runtime_error("Invalid triggering_event, expected EvReceive"));
        }
    }
    catch(std::exception& e)
    {
        goby::glog.is(goby::common::logger::WARN) && goby::glog << "Invalid data received, ignoring: " << e.what() << std::endl;
        post_event(EvReceiveComplete());
    }
}

void goby::acomms::benthos_fsm::ReceiveData::in_state_react( const EvRxSerial& e)
{
    try 
    {
        std::string in = e.line;
        boost::trim(in);
        const std::string source = "Source";
        const std::string crc = "CRC";
    
    
        if(in == "<EOP>")
        {
            const benthos::protobuf::ReceiveStatistics& rx_stats = rx_msg_.GetExtension(benthos::protobuf::receive_stat);

            if(rx_stats.crc() == benthos::protobuf::ReceiveStatistics::CRC_PASS)
                parse_benthos_modem_message(encoded_bytes_, &rx_msg_);
            
            context<BenthosATM900FSM>().received().push_back(rx_msg_);
            
            post_event(EvReceiveComplete());
        }
        else if(encoded_bytes_.size() < reported_size_)
        {
            // assume more bytes
            boost::erase_all(in, " ");
            encoded_bytes_ += goby::util::hex_decode(in);
        }
        else if(in.compare(0, source.size(), source) == 0)
        {
            //  Source:001  Destination:002
            std::vector<std::string> src_dest;
            boost::split(src_dest, in, boost::is_any_of(" "), boost::token_compress_on);
            if(src_dest.size() != 2)
            {
                throw(std::runtime_error("Invalid source/dest string, expected \"Source:NNN Destination: MMM\""));
            }

            enum { SOURCE_ID_START = 7, DEST_ID_START = 12 };
            for(int i = 0; i < 2; ++i)
            {
                std::string id_str = src_dest[i].substr(i == 0 ? SOURCE_ID_START : DEST_ID_START);
                boost::trim_left_if(id_str, boost::is_any_of("0"));
                int id = boost::lexical_cast<unsigned>(id_str);
                if(i == 0) rx_msg_.set_src(id);
                else if(i == 1) rx_msg_.set_dest(id);
            }
        }
        else if(in.compare(0, crc.size(), crc) == 0)
        {
            benthos::protobuf::ReceiveStatistics& rx_stats = *rx_msg_.MutableExtension(benthos::protobuf::receive_stat);
                
            // CRC:Pass MPD:03.2 SNR:31.3 AGC:91 SPD:+00.0 CCERR:013
            std::vector<std::string> stats;
            boost::split(stats, in, boost::is_any_of(" "), boost::token_compress_on);
            
            enum StatField
            {
                STAT_CRC = 0,
                STAT_MPD = 1,
                STAT_AGC = 2,
                STAT_SNR = 3,
                STAT_SPD = 4,
                STAT_CCERR = 5
            };

            // use int instead of enum to satisfy compiler (enum is local type)
            std::map<std::string, int> statmap;
            statmap.insert(std::make_pair("CRC", (int)STAT_CRC));
            statmap.insert(std::make_pair("MPD", (int)STAT_MPD));
            statmap.insert(std::make_pair("SNR", (int)STAT_SNR));
            statmap.insert(std::make_pair("AGC", (int)STAT_AGC));
            statmap.insert(std::make_pair("SPD", (int)STAT_SPD));
            statmap.insert(std::make_pair("CCERR", (int)STAT_CCERR));
            
            
            for(unsigned i = 0; i < stats.size(); ++i)
            {
                std::string::size_type col_pos = stats[i].find(":");
                if(col_pos == std::string::npos)
                    continue;
                
                std::string key_str = stats[i].substr(0, col_pos);

                std::map<std::string, int>::const_iterator it = statmap.find(key_str);
                if(it == statmap.end())
                    continue;
                
                StatField field = static_cast<StatField>(it->second);
                std::string val_str = stats[i].substr(col_pos+1);
                boost::trim(val_str);
                boost::trim_left_if(val_str, boost::is_any_of("+0"));
                if(val_str.empty()) val_str = "0";
                float val = 0;
                if(field != STAT_CRC) val = boost::lexical_cast<float>(val_str);
                switch(field)
                {
                    case STAT_CRC:
                        if(val_str == "Pass")
                        {
                            rx_stats.set_crc(benthos::protobuf::ReceiveStatistics::CRC_PASS);
                        }
                        else
                        {
                            rx_stats.set_crc(benthos::protobuf::ReceiveStatistics::CRC_FAIL);
                        }
                        break;
                    case STAT_MPD:
                        rx_stats.set_multipath_delay_with_units(val*boost::units::si::milli*boost::units::si::seconds);
                        break;
                    case STAT_SNR:
                        rx_stats.set_snr(val);
                        break;
                    case STAT_AGC:
                        rx_stats.set_auto_gain_control(val);
                        break;
                    case STAT_SPD:
                    {
                        boost::units::metric::knot_base_unit::unit_type knots;
                        rx_stats.set_relative_speed_with_units(val*knots);
                        break;
                    }
                    case STAT_CCERR:
                        rx_stats.set_corrected_channel_error(val);
                        break;
                }
            }
            
        }
    }
    catch(std::exception& e)
    {
        goby::glog.is(goby::common::logger::WARN) && goby::glog << "Invalid data received, ignoring. Reason: " << e.what() << std::endl;
        post_event(EvReceiveComplete());
    }
    
}

void goby::acomms::benthos_fsm::Range::in_state_react( const EvRxSerial& e)
{
    try 
    {
        std::string in = e.line;
        boost::trim(in);

        static const std::string tx_time = "TX time";
        static const std::string range = "Range";

        if(!context<Command>().at_out().empty())
        {
            const std::string& last_cmd = context<Command>().at_out().front().second;
            if(last_cmd.substr(0, 3) == "ATR" && boost::iequals(in.substr(0, tx_time.size()), tx_time))
            {
                context<Command>().at_out().pop_front();
            }
        }
        
        if(in.compare(0, range.size(), range) == 0)
        {
            protobuf::ModemTransmission range_msg;

            // Range 1 to 2 : 1499.6 m  (Round-trip  1999.5 ms) speed  0.0 m/s
            // ^-range_pos  ^- col_pos  ^- rt_start_pos     ^- rt_end_pos 

            const std::string roundtrip = "(Round-trip";
            const std::string ms = "ms)";
            
            std::string::size_type range_pos = in.find(range);
            std::string::size_type col_pos = in.find(":");
            std::string::size_type rt_start_pos = in.find(roundtrip);
            std::string::size_type rt_end_pos = in.find(ms);

            if(range_pos == std::string::npos || col_pos == std::string::npos || rt_start_pos == std::string::npos || rt_end_pos == std::string::npos)
                throw(std::runtime_error("Invalid format for range string"));

            // "1 to 2"
            std::string src_to_dest = in.substr(range_pos + range.size(), col_pos-(range_pos + range.size()));
            boost::trim(src_to_dest);
            std::vector<std::string> src_dest;
            boost::split(src_dest, src_to_dest, boost::is_any_of(" to"), boost::token_compress_on);
            if(src_dest.size() != 2)
            {
                throw(std::runtime_error("Invalid source/dest string, expected \"Range 1 to 2\""));
            }

            range_msg.set_src(boost::lexical_cast<unsigned>(src_dest[0]));
            range_msg.set_dest(boost::lexical_cast<unsigned>(src_dest[1]));

            range_msg.set_type(protobuf::ModemTransmission::DRIVER_SPECIFIC);
            range_msg.SetExtension(benthos::protobuf::type, benthos::protobuf::BENTHOS_TWO_WAY_PING);

            std::string rt_ms = in.substr(rt_start_pos + roundtrip.size(), rt_end_pos - (rt_start_pos + roundtrip.size()));
            boost::trim(rt_ms);
            
            range_msg.MutableExtension(benthos::protobuf::ranging_reply)->set_one_way_travel_time_with_units(boost::lexical_cast<double>(rt_ms)/2*boost::units::si::milli*boost::units::si::seconds);
            
            
            context<BenthosATM900FSM>().received().push_back(range_msg);
            post_event(EvRangingComplete());
        }
    }
    catch(std::exception& e)
    {
        goby::glog.is(goby::common::logger::WARN) && goby::glog << "Invalid ranging data received, ignoring. Reason: " << e.what() << std::endl;
        post_event(EvRangingComplete());
    }
}


void goby::acomms::benthos_fsm::Command::in_state_react( const EvTxSerial& )
{
    double now = goby_time<double>();

    static const std::string atd = "ATD";
    
    if(!at_out_.empty())
    {
        double timeout = COMMAND_TIMEOUT_SECONDS;

        if((at_out_.front().first.last_send_time_ + timeout) < now)
        {
            std::string at_command;
            if(at_out_.front().second == "+++")
            {
                // Benthos implementation of Hayes requires 1 second pause *before* +++
                usleep(1300000);
                // insert \r after +++ so that we accidentally send +++ during command mode, this is recognized as an (invalid) command
                at_command = at_out_.front().second + "\r";
            }
            else
            {
                at_command = at_out_.front().second + "\r";
            }
            
            if(++at_out_.front().first.tries_ > RETRIES_BEFORE_RESET)
            {
                glog.is(DEBUG1) && glog << group("benthosatm900") << warn << "No valid response after " << RETRIES_BEFORE_RESET << " tries. Resetting state machine" << std::endl;
                post_event(EvReset());
            }
            else
            {
                context<BenthosATM900FSM>().serial_tx_buffer().push_back(at_command);
                at_out_.front().first.last_send_time_ = now;
            }
        }
        
    }
}



void goby::acomms::benthos_fsm::TransmitData::in_state_react( const EvTxSerial& )
{
    boost::circular_buffer<protobuf::ModemTransmission>& data_out = context<BenthosATM900FSM>().data_out();
    if(!data_out.empty())
    {
        // frame message
        std::string packet;
        serialize_benthos_modem_message(&packet, data_out.front());
        context<BenthosATM900FSM>().serial_tx_buffer().push_back(packet);

        data_out.pop_front();
    }
}

void goby::acomms::benthos_fsm::TransmitData::in_state_react( const EvAck & e)
{
    static const std::string forwarding_delay_up = "Forwarding Delay Up";
    if(e.response_.compare(0, forwarding_delay_up.size(), forwarding_delay_up) == 0)
    {
        post_event(EvTransmitBegun());
    }
}    


void goby::acomms::benthos_fsm::Command::in_state_react( const EvAck & e)
{
    bool valid = false;
    if(!at_out().empty())
    {
        const std::string& last_cmd = at_out().front().second;
        if(last_cmd.size() > 0 && (e.response_ == "OK"))
        {
            if(last_cmd == "ATH")
                post_event(EvNoCarrier());
        }

        static const std::string connect = "CONNECT";    
        static const std::string user = "user";

        if(e.response_ == "OK")
        {
            valid = true;
        }
        else if(e.response_.compare(0, connect.size(), connect) == 0 && last_cmd.substr(0, 3) != "+++")
        {
            valid = true;
        }
        else if(e.response_ == "Command '+++' not found" && last_cmd.substr(0, 3) == "+++") // response to '+++' during command mode, so this is OK
        {
            valid = true;
        }
        else if(last_cmd.substr(0, 3) == "+++" && (e.response_.compare(0, user.size(), user) == 0 || e.response_.empty()))
        {
            // no response in command mode other than giving us a new user:N> prompt or sometimes just an empty string
            valid = true;
        }
        else if(last_cmd.substr(0, 3) == "ATL" && e.response_.find("Lowpower") != std::string::npos)
        {
            post_event(EvLowPower());
            valid = true;
        }
        else if(last_cmd.size() > 0) // deal with varied CLAM responses
        {
            std::string::size_type eq_pos = last_cmd.find('=');
            static const std::string cfg_store = "cfg store";
            static const std::string date_cmd = "date -t";
            
            if(last_cmd[0] == '@' && eq_pos != std::string::npos && boost::iequals(last_cmd.substr(1, eq_pos-1), e.response_.substr(0, eq_pos-1)))
            {
                // configuration sentences
                // e.g. last_cmd: "@P1EchoChar=Dis"
                //            in: "P1EchoChar      | Dis"
                valid = true;
            }
            else if(e.response_ == "Configuration stored." && boost::iequals(last_cmd.substr(0, cfg_store.size()), cfg_store))
            {
                valid = true;
            }
            else if(boost::iequals(last_cmd.substr(0, date_cmd.size()), date_cmd) && e.response_.find(last_cmd.substr(date_cmd.size(), 8)) != std::string::npos) // match time in response e.g. 19:04:26
            {
                valid = true;
            }
            
        }
        
        if(valid)
        {
            glog.is(DEBUG2) && glog << "Popping: " << at_out().front().second << std::endl;
            at_out().pop_front();
            if(at_out().empty())
                post_event(EvAtEmpty());
        }
    }

    if(!valid)
    {
        glog.is(DEBUG1) && glog << group("benthosatm900") << "Ignoring: '" << e.response_ << "'" << std::endl;
    }
}
