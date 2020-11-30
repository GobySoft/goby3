/************************************************************/
/*    NAME: Thomas McCabe                                   */
/*    ORGN: Mission Systems Pty Ltd                         */
/*    FILE: Popoto.cpp                                      */
/*    DATE: Aug 20 2020                                    */
/************************************************************/

/* Copyright (c) 2020 mission systems pty ltd */

#include "popoto_driver.h"
#include <iostream>

#include "driver_exception.h"
#include "goby/util/debug_logger.h"
#include "goby/util/protobuf/io.h"
#include "goby/util/thirdparty/nlohmann/json.hpp"

using goby::glog;
using namespace goby::util::logger;
using json = nlohmann::json;

goby::acomms::popotoDriver::popotoDriver() {}
goby::acomms::popotoDriver::~popotoDriver() {}

void goby::acomms::popotoDriver::startup(const protobuf::DriverConfig& cfg)
{
    driver_cfg_ = cfg;

    // Popoto specific start up strings
    int modem_p =  popoto_driver_cfg().modem_power();
    int payload_mode = popoto_driver_cfg().payload_mode();
    int start_timeout = popoto_driver_cfg().start_timeout();
  
    // Set the default baud 
    if (!driver_cfg_.has_serial_baud())
        driver_cfg_.set_serial_baud(DEFAULT_BAUD);

    glog.is(DEBUG1) && glog << group(glog_out_group()) << "PopotoDriver: Starting modem..."
                            << std::endl;
    ModemDriverBase::modem_start(driver_cfg_);
    
    
    // -------------------------- set the Popoto CFG params in the right format ------------
    std::stringstream raw;
    raw << "setvaluef TxPowerWatts " + std::to_string(modem_p) << "\n";
    signal_and_write(raw.str());
    
    raw.str(""); // clear the string stream
    raw << "setvaluei PayloadMode " + std::to_string(payload_mode) << "\n";
    signal_and_write(raw.str());
    
    raw.str(""); 
    raw << "setvaluei LedEnable " + std::to_string(0) << "\n";
    signal_and_write(raw.str());

    raw.str(""); 
    raw << "setvaluei LocalID " << driver_cfg_.modem_id() << "\n";
    signal_and_write(raw.str());
    
    // Poll the modem temp and battery voltage
    signal_and_write("getvaluef BatteryVoltage\n");
    signal_and_write("getvaluef Temp_Ambient\n");

    // Check if modem has started
    std::string in;
    int startup_elapsed_ms = 0;
    
    while (!modem_read(&in))
    {   
        usleep(100000); // 100 ms
        startup_elapsed_ms += 100;

        if (startup_elapsed_ms/1000 >= start_timeout)
            throw(ModemDriverException("Modem physical connection failed to startup.",
                                       protobuf::ModemDriverStatus::STARTUP_FAILED));
    }

    std::cout << "Modem " << driver_cfg_.modem_id()  << " initialized OK." << std::endl;
}

void goby::acomms::popotoDriver::shutdown()
{  

    ModemDriverBase::modem_close();
}

// --------------------------- Outgoing msgs ------------------------------------------------
void goby::acomms::popotoDriver::handle_initiate_transmission(
    const protobuf::ModemTransmission& orig_msg)
{
    protobuf::ModemTransmission msg = orig_msg;
    // Poll the modem temp and battery voltage before each transmission
    signal_and_write("getvaluef BatteryVoltage\n");
    signal_and_write("getvaluef Temp_Ambient\n");
    
    switch (msg.type())
    {
        case protobuf::ModemTransmission::DATA:
        {
            //msg.set_max_frame_bytes(1024); // Might not need to set this
            ModemDriverBase::signal_modify_transmission(&msg);

            // no data given to us, let's ask for some
            if (!msg.has_frame_start())
                msg.set_frame_start(next_frame_);

            if (msg.frame_size() == 0)
                ModemDriverBase::signal_data_request(&msg);

            next_frame_ += msg.frame_size();

            // Bitrates with Popoto modem: map these onto 0-5

            // Rates not implemented?
            // const char *setRateSpeed[] = {"setRate80\n", "setRate640\n", "setRate1280\n", "setRate2560\n", "setRate5120\n", "setRate10240\n"};
            if (msg.frame(0).size() > 0) 
            {
                glog.is(DEBUG1) && glog << group(glog_out_group()) << "We were asked to transmit from "
                                    << msg.src() << " to " << msg.dest() << " at bitrate code "
                                    << msg.rate() << std::endl;
                glog.is(DEBUG1) && glog << group(glog_out_group()) << "Sending these data now: " << msg.frame(0)
                                    << std::endl;
                
                send(msg);
            }
            break;
        }

        case protobuf::ModemTransmission::DRIVER_SPECIFIC:
        {
            switch (msg.GetExtension(popoto::protobuf::transmission).type())
            {
                case popoto::protobuf::POPOTO_TWO_WAY_RANGE:
                    send_range_request(msg.dest()); // send a ranging message
                    break;

                case popoto::protobuf::POPOTO_PLAY_FILE:
                    play_file(msg);
                    break;

                case popoto::protobuf::POPOTO_TWO_WAY_PING:
                    send_ping(msg); 
                    break;

                case popoto::protobuf::POPOTO_DEEP_SLEEP:
                    popoto_sleep(); 
                    break;

                case popoto::protobuf::POPOTO_WAKE:
                    send_wake(); // the wake will just be a ping for the moment 
                    break;

                default:
                glog.is(DEBUG1) &&
                    glog << group(glog_out_group()) << warn
                         << "Not initiating transmission because we were given an invalid "
                            "DRIVER_SPECIFIC transmission type for the Popoto Modem"
                         << msg << std::endl;
                break;
            }
        }

        default:
            glog.is(WARN) && glog << group(glog_out_group()) << "Unsupported transmission type: "
                                  << protobuf::ModemTransmission::TransmissionType_Name(msg.type())
                                  << std::endl;
            break;
    }
}
//--------------------------------------- send_wake ------------------------------------------------------------
// Send a wake command to the other modem, this can be any message so using a ping which can be changed if needed
void goby::acomms::popotoDriver::send_wake(void)
{

    std::stringstream message;
    message << "ping " << popoto_driver_cfg().modem_power() << std::endl; 
    
    signal_and_write(message.str());
}
//--------------------------------------- popoto_sleep ---------------------------------------------------------
// Send a sleep command to the current modem
void goby::acomms::popotoDriver::popoto_sleep(void)
{
    std::cerr << "Modem will now sleep: " << std::endl; 
    signal_and_write("powerdown\n"); //This will put the modem into deep sleep mode to wake up on next acoustic signal
}

//--------------------------------------- play_file ------------------------------------------------------------
// Play a file from the modems directory
void goby::acomms::popotoDriver::play_file(protobuf::ModemTransmission& msg)
{
    std::cerr << msg.DebugString() << std::endl; 

    std::stringstream message;
    message << "playstart "
                <<  msg.GetExtension(popoto::protobuf::transmission).file_location() << " "
                << msg.GetExtension(popoto::protobuf::transmission).transmit_power() << "\n";

    // send over the wire 
    signal_and_write("playstop\n"); // need to make sure nothing else is playing
    signal_and_write(message.str());

}
//--------------------------------------- send_ping ------------------------------------------------------------
// Send a ping
void goby::acomms::popotoDriver::send_ping(protobuf::ModemTransmission& msg)
{
    std::cerr << msg.DebugString() << std::endl; 

    std::stringstream message;
    message << "ping " << msg.GetExtension(popoto::protobuf::transmission).transmit_power() << "\n";
    
    signal_and_write(message.str());
          
}

// ------------------------------------- Send -------------------------------------------------
void goby::acomms::popotoDriver::send(protobuf::ModemTransmission& msg)
{
    int dest = msg.dest();
    
    std::stringstream raw1; 
    raw1 << "setvaluei RemoteID " << dest << "\n";
    signal_and_write(raw1.str());
    
    std::stringstream out_msg;
    int d = msg.frame_start(); 
    out_msg << "ACK: " << d;
    protobuf::ModemRaw raw_out;
    raw_out.set_raw(out_msg.str());
    ModemDriverBase::signal_raw_incoming(raw_out);

    // Pass the binary packet to comma to convert it to csv
    char buf[1024];
    std::string ss;
    msg.clear_slot_seconds();
    msg.clear_time();    
    msg.clear_slot_index();

    if( msg.type() != protobuf::ModemTransmission::ACK)
    {
        msg.clear_type();
        msg.clear_ack_requested();
    }

    msg.SerializeToString(&ss);
    std::memcpy(buf, ss.data(), ss.size());

    std::string jsonStr;
    size_t buf_size = ss.size();
    jsonStr = binary_to_json(buf,buf_size);
    // To send a bin msg it needs to be in 8 bit CSV values
    std::stringstream raw;
    raw << "transmitJSON {\"Payload\":{\"Data\":[" << jsonStr<< "]}}" << "\n";

    // Send the raw string to terminal for debugging
    std::cout << raw.str() << std::endl;

    // Send over the wire
    signal_and_write(raw.str());

}


// ---------------------------- Ranging ----------------------------------------------------
void goby::acomms::popotoDriver::send_range_request(int dest)
{

    std::stringstream raw; 
    raw << "setvaluei RemoteID " << dest << "\n";
    signal_and_write(raw.str());

    // A range messages needs to be formated with 'range txPower' we will use the default power from .moos launch file
    std::stringstream range;
    range << "range " << popoto_driver_cfg().modem_power() << "\n";

    // Send over the wire
    signal_and_write(range.str());

}

// --------------------------- Incoming msgs ------------------------------------------------
void goby::acomms::popotoDriver::do_work()
{
    std::string in;
    while (modem_read(&in))
    {
        try
        {
            // let others know about the raw feed. Can remove this if not needed
            protobuf::ModemRaw raw;
            raw.set_raw(in);
            ModemDriverBase::signal_raw_incoming(raw);

            // Remove VT100 sequences (if they exist) and popoto prompt
            std::cerr << "This is then recieved string: " << in << std::endl;
            in = StripString(in, "Popoto->");
            in = StripString(in, VT100_BOLD_ON);
            in = StripString(in, VT100_BOLD_OFF);

            std::string binMsg;
            if(json::accept(in))
            {
                binMsg = ProcessJSON(in);
                protobuf::ModemTransmission msg;
                if (!binMsg.empty()){

                    msg.ParseFromString(binMsg); //why don't the other drivers use this? 
                    glog.is(DEBUG1) && glog << group(glog_in_group()) << in << std::endl;
                    glog.is(DEBUG1) && glog << group(glog_in_group()) << "received: " << msg
                                            << std::endl;
                    
                    if( msg.type() == protobuf::ModemTransmission::ACK)
                    {   
                        std::stringstream out_msg;
                        int d = msg.acked_frame(0);
                        out_msg << "GOT ACK FROM FRAME " << d;
                        msg.set_src(sender_id_);
                        msg.set_dest(driver_cfg_.modem_id());
                        protobuf::ModemRaw raw_out;
                        raw_out.set_raw(out_msg.str());
                        ModemDriverBase::signal_raw_incoming(raw_out);

                    } else {
                        // make any acks
                        protobuf::ModemTransmission ack;
                        ack.set_type(goby::acomms::protobuf::ModemTransmission::ACK);
                        for (int i = msg.frame_start(), n = msg.frame_size() + msg.frame_start(); i < n; ++i)
                            ack.add_acked_frame(i);
                        
                        send(ack); // reply with the ack msg

                    }
                    ModemDriverBase::signal_receive(msg);
                }
            }   
        }
        catch (std::exception& e)
        {
            glog.is(WARN) && glog << "Bad line: " << in << std::endl;
            glog.is(WARN) && glog << "Exception: " << e.what() << std::endl;    
        }
    }
}
// --------------------------- Write over the wire ------------------------------------------------
void goby::acomms::popotoDriver::signal_and_write(const std::string& raw)
{
    protobuf::ModemRaw raw_msg;
    raw_msg.set_raw(raw);
    ModemDriverBase::signal_raw_outgoing(raw_msg);

    glog.is(DEBUG1) && glog << group(glog_out_group()) << boost::trim_copy(raw) << std::endl;
    ModemDriverBase::modem_write(raw);
}

// Converts the dccl binary to the required comma seperated bytes 
std::string binary_to_json( const char* buf, size_t num_bytes )
{
    std::string output;

    for (int i = 0; i < num_bytes; i++){
        output.append(std::to_string((uint8_t) buf[i]));
        if(i < num_bytes-1) {
            output.append(",");
        }
    }

    return output;
}

// Convert csv values back to dccl binary for the dccl codec to decode
std::string json_to_binary( const json &element) {
    std::string output;
   
   for(auto &subel : element) {
        output.append(1, (char) ((uint8_t)subel));
    }
     
    return output;
}

// Decode Popoto header
void  goby::acomms::popotoDriver::DecodeHeader(std::vector<uint8_t> data)
{
    std::string type;

    // Process binary payload data
    switch(data[0]) {

        case DATA_MESSAGE:
            type = "Data message";
            break;

        case RANGE_RESPONSE:
            type = "Range response";
            break;

        case RANGE_REQUEST:
            type = "Range_request";
            break;

        case STATUS:
            type = "Status message";
            break;

        default:
            std::cout << "Unknown message type" << std::endl; 
            break;
    }

    int sender  = data[1];
    std::string receiver = std::to_string(data[2]);
    std::string tx_power = std::to_string(data[3]);
    sender_id_ = sender; 
    std::cout << type << " from " << sender << " to " << receiver << " at tx power: " << tx_power << std::endl; 
}

// The only msg that is important for the dccl driver is the header and data. We will just print everything else to terminal for the moment
std::string goby::acomms::popotoDriver::ProcessJSON(std::string message)
{
    json j = json::parse(message);
    json::iterator it = j.begin();
    std::string label = it.key();
    std::string str;
    
    protobuf::ModemRaw raw;
    raw.set_raw(message);

    if (label == "Header") {

      DecodeHeader(j["Header"]);
    
    } else if (label == "Data") {
        
        str = json_to_binary(j["Data"]);
        return str;
    
    } else if (label == "Alert"){
        
        std::cout << j["Alert"] << std::endl;
        ModemDriverBase::signal_raw_incoming(raw);

    } else if (label == "SNRdB"){

        std::cout << j["SNRdB"] << std::endl;
        ModemDriverBase::signal_raw_incoming(raw);
        
    } else if (label == "DopplerVelocity") {
    
        std::cout << j["DopplerVelocity"] << std::endl;
        ModemDriverBase::signal_raw_incoming(raw);

    } else if (label == "Info") {

      std::cout << j["Info"] << std::endl;
      ModemDriverBase::signal_raw_incoming(raw);

    } else{
        return "";
    }
    
    return str;
}
// Remove popoto trash from the incoming serial string
std::string StripString(std::string in, std::string p) {
  std::string out = in;
  std::string::size_type n = p.length();
  for (std::string::size_type i = out.find(p);
      i != std::string::npos;
      i = out.find(p))
      out.erase(i, n);

  return out;
}
