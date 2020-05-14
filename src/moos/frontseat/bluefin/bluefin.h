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

#ifndef BluefinFrontSeat20130220H
#define BluefinFrontSeat20130220H

#include <boost/bimap.hpp>

#include "goby/util/linebasedcomms/nmea_sentence.h"
#include "goby/util/linebasedcomms/tcp_client.h"

#include "goby/moos/frontseat/frontseat.h"

#include "goby/moos/frontseat/bluefin/bluefin.pb.h"
#include "goby/moos/frontseat/bluefin/bluefin_config.pb.h"

namespace goby
{
namespace moos
{

class BluefinFrontSeat : public FrontSeatInterfaceBase
{
  public:
    BluefinFrontSeat(const goby::apps::moos::protobuf::iFrontSeatConfig& cfg);

  private: // virtual methods from FrontSeatInterfaceBase
    void loop();

    void send_command_to_frontseat(const goby::moos::protobuf::CommandRequest& command);
    void send_data_to_frontseat(const goby::moos::protobuf::FrontSeatInterfaceData& data);
    void send_raw_to_frontseat(const goby::moos::protobuf::FrontSeatRaw& data);

    goby::moos::protobuf::FrontSeatState frontseat_state() const { return frontseat_state_; }

    bool frontseat_providing_data() const { return frontseat_providing_data_; }

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
    const apps::moos::protobuf::BluefinFrontSeatConfig bf_config_;
    goby::util::TCPClient tcp_;
    bool frontseat_providing_data_;
    goby::time::SystemClock::time_point last_frontseat_data_time_;
    goby::moos::protobuf::FrontSeatState frontseat_state_;
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
    goby::moos::protobuf::NodeStatus status_;

    // maps command type to outstanding request, if response is requested
    std::map<goby::moos::protobuf::BluefinExtraCommands::BluefinCommand,
             goby::moos::protobuf::CommandRequest>
        outstanding_requests_;

    // maps status expire time to payload status
    std::multimap<goby::time::MicroTime, goby::moos::protobuf::BluefinExtraData::PayloadStatus>
        payload_status_;

    // maps speed to rpm value
    std::map<double, int> speed_to_rpm_;
};
} // namespace moos
} // namespace goby

#endif
