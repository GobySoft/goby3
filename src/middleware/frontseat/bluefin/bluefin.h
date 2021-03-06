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

#ifndef GOBY_MIDDLEWARE_FRONTSEAT_BLUEFIN_BLUEFIN_H
#define GOBY_MIDDLEWARE_FRONTSEAT_BLUEFIN_BLUEFIN_H

#include <deque>  // for deque
#include <map>    // for map
#include <string> // for string

#include <boost/bimap/bimap.hpp> // for bimap

#include "goby/middleware/frontseat/bluefin/bluefin.pb.h"        // for Blu...
#include "goby/middleware/frontseat/bluefin/bluefin_config.pb.h" // for Blu...
#include "goby/middleware/frontseat/interface.h"                 // for Int...
#include "goby/middleware/protobuf/frontseat.pb.h"               // for Fro...
#include "goby/middleware/protobuf/frontseat_data.pb.h"          // for Nod...
#include "goby/time/system_clock.h"                              // for Sys...
#include "goby/time/types.h"                                     // for Mic...
#include "goby/util/linebasedcomms/nmea_sentence.h"              // for NME...
#include "goby/util/linebasedcomms/tcp_client.h"                 // for TCP...

namespace goby
{
namespace middleware
{
namespace frontseat
{
namespace protobuf
{
class Config;
} // namespace protobuf

class Bluefin : public InterfaceBase
{
  public:
    Bluefin(const protobuf::Config& cfg);

  private: // virtual methods from InterfaceBase
    void loop() override;

    void send_command_to_frontseat(const protobuf::CommandRequest& command) override;
    void send_data_to_frontseat(const protobuf::InterfaceData& data) override;
    void send_raw_to_frontseat(const protobuf::Raw& data) override;

    protobuf::FrontSeatState frontseat_state() const override { return frontseat_state_; }

    bool frontseat_providing_data() const override { return frontseat_providing_data_; }

  private: // internal non-virtual methods
    void load_nmea_mappings();
    void initialize_huxley();
    void append_to_write_queue(const goby::util::NMEASentence& nmea);
    void remove_from_write_queue();

    void check_send_heartbeat();
    void try_send();
    void try_receive();
    void write(const goby::util::NMEASentence& nmea);
    void process_receive(const goby::util::NMEASentence& nmea);

    void bfack(const goby::util::NMEASentence& nmea);
    void bfnvr(const goby::util::NMEASentence& nmea);
    void bfsvs(const goby::util::NMEASentence& nmea);
    void bfrvl(const goby::util::NMEASentence& nmea);
    void bfnvg(const goby::util::NMEASentence& nmea);
    void bfmsc(const goby::util::NMEASentence& nmea);
    void bfsht(const goby::util::NMEASentence& nmea);
    void bfmbs(const goby::util::NMEASentence& nmea);
    void bfboy(const goby::util::NMEASentence& nmea);
    void bftrm(const goby::util::NMEASentence& nmea);
    void bfmbe(const goby::util::NMEASentence& nmea);
    void bftop(const goby::util::NMEASentence& nmea);
    void bfdvl(const goby::util::NMEASentence& nmea);
    void bfmis(const goby::util::NMEASentence& nmea);
    void bfctd(const goby::util::NMEASentence& nmea);
    void bfctl(const goby::util::NMEASentence& nmea);

    std::string unix_time2nmea_time(goby::time::SystemClock::time_point time);

  private:
    const protobuf::BluefinConfig bf_config_;
    goby::util::TCPClient tcp_;
    bool frontseat_providing_data_;
    goby::time::SystemClock::time_point last_frontseat_data_time_;
    protobuf::FrontSeatState frontseat_state_;
    goby::time::SystemClock::time_point next_connect_attempt_time_;

    goby::time::SystemClock::time_point last_write_time_;
    std::deque<goby::util::NMEASentence> out_;
    std::deque<goby::util::NMEASentence> pending_;
    bool waiting_for_huxley_;
    unsigned nmea_demerits_;
    unsigned nmea_present_fail_count_;

    goby::time::SystemClock::time_point last_heartbeat_time_;

    enum TalkerIDs
    {
        TALKER_NOT_DEFINED = 0,
        BF,
        BP
    };

    enum SentenceIDs
    {
        SENTENCE_NOT_DEFINED = 0,
        MSC,
        SHT,
        BDL,
        SDL,
        TOP,
        DVT,
        VER,
        NVG,
        SVS,
        RCM,
        RDP,
        RVL,
        RBS,
        MBS,
        MBE,
        MIS,
        ERC,
        DVL,
        DV2,
        IMU,
        CTD,
        RNV,
        PIT,
        CNV,
        PLN,
        ACK,
        TRM,
        LOG,
        STS,
        DVR,
        CPS,
        CPR,
        TRK,
        RTC,
        RGP,
        RCN,
        RCA,
        RCB,
        RMB,
        EMB,
        TMR,
        ABT,
        KIL,
        MSG,
        RMP,
        SEM,
        NPU,
        CPD,
        SIL,
        BOY,
        SUS,
        CON,
        RES,
        SPD,
        SAN,
        GHP,
        GBP,
        RNS,
        RBO,
        CMA,
        NVR,
        TEL,
        CTL,
        DCL,
        VEL
    };

    std::map<std::string, TalkerIDs> talker_id_map_;
    boost::bimap<std::string, SentenceIDs> sentence_id_map_;
    std::map<std::string, std::string> description_map_;

    // the current status message we're building up
    protobuf::NodeStatus status_;

    // maps command type to outstanding request, if response is requested
    std::map<protobuf::BluefinExtraCommands::BluefinCommand, protobuf::CommandRequest>
        outstanding_requests_;

    // maps status expire time to payload status
    std::multimap<goby::time::MicroTime, protobuf::BluefinExtraData::PayloadStatus> payload_status_;

    // maps speed to rpm value
    std::map<double, int> speed_to_rpm_;
};
} // namespace frontseat
} // namespace middleware
} // namespace goby

#endif
