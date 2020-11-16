/************************************************************/
/*    NAME: Thomas McCabe                                   */
/*    ORGN: Mission Systems Pty Ltd                         */
/*    FILE: Popoto.h                                        */
/*    DATE: Aug 20 2020                                     */
/************************************************************/

/* Copyright (c) 2020 mission systems pty ltd */

#ifndef popotoDriver2020
#define popotoDriver2020

#include "goby/time.h"

#include <comma/application/command_line_options.h>
#include <comma/csv/format.h>
#include <iostream>
#include <string>
#include "goby/acomms/modemdriver/driver_base.h"
#include "goby/acomms/protobuf/popoto_driver.pb.h"

#define VT100_BOLD_ON  "\x1b[1m"
#define VT100_BOLD_OFF "\x1b[0m"
#define DEFAULT_BAUD 115200

// Popoto header types
#define DATA_MESSAGE	0
#define RANGE_RESPONSE	128
#define RANGE_REQUEST	129	
#define STATUS			130

namespace goby
{
namespace acomms
{
class popotoDriver : public ModemDriverBase
{
  public:
    popotoDriver();
    ~popotoDriver();

    std::uint32_t next_frame_{0};

    void startup(const protobuf::DriverConfig& cfg) override;
    void shutdown() override;
    void do_work() override;
    void handle_initiate_transmission(const protobuf::ModemTransmission& m) override;
    void send(protobuf::ModemTransmission& msg); 
    void play_file(protobuf::ModemTransmission& msg); 
    void send_ping(protobuf::ModemTransmission& msg); 
    void send_wake(void); 
    void send_range_request(int dest); 
    void popoto_sleep(void);

  private:
    void parse_in(const std::string& in, std::map<std::string, std::string>* out);
    void signal_and_write(const std::string& raw);
    void DecodeHeader(std::vector<uint8_t> data);
    std::string ProcessJSON(std::string message);
    
    const popoto::protobuf::Config& popoto_driver_cfg() const
    {
        return driver_cfg_.GetExtension(popoto::protobuf::config);
    }
    

  private:
    protobuf::DriverConfig driver_cfg_;
    int sender_id_;
    std::map<int, int> rate_to_bytes_;
};
} // namespace acomms
} // namespace goby

std::string binary_to_json( const char* buf, size_t num_bytes );
std::string ProcessJSON(std::string message);
std::string StripString(std::string in, std::string p);

#endif
