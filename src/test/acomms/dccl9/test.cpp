// Copyright 2009-2017 Toby Schneider (http://gobysoft.org/index.wt/people/toby)
//                     GobySoft, LLC (2013-)
//                     Massachusetts Institute of Technology (2007-2014)
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

// tests usage of a custom DCCL ID codec
// including Legacy CCL

#include "goby/acomms/dccl.h"
#include "goby/acomms/acomms_helpers.h"
#include "goby/common/time.h"
#include "goby/util/primitive_types.h"
#include "goby/util/as.h"
#include "goby/util/sci.h"
#include "goby/util/dynamic_protobuf_manager.h"
#include "dccl/ccl/protobuf/ccl.pb.h"
#include "test.pb.h"

using goby::acomms::operator<<;

class MicroModemMiniPacketDCCLIDCodec : public goby::acomms::DCCLTypedFixedFieldCodec<goby::uint32>
{
private:
    goby::acomms::Bitset encode(const goby::uint32& wire_value);
    
    goby::acomms::Bitset encode()
        { return encode(MINI_ID_OFFSET); }
    
    goby::uint32 decode(goby::acomms::Bitset* bits)
        { return bits->to_ulong() + MINI_ID_OFFSET; }
    
    unsigned size()
        { return MINI_ID_SIZE; }
    
    void validate()
        { }
    

    // Add this value when decoding to put us safely in our own namespace
    // from the normal default DCCL Codec
    enum { MINI_ID_OFFSET = 1000000 };    
    enum { MINI_ID_SIZE = 6 };
};


bool double_cmp(double a, double b, int precision)
{
    int a_whole = a;
    int b_whole = b;

    int a_part = (a-a_whole)*pow(10.0, precision);
    int b_part = (b-b_whole)*pow(10.0, precision);
    
    return (a_whole == b_whole) && (a_part == b_part);
}

goby::acomms::Bitset MicroModemMiniPacketDCCLIDCodec::encode(const goby::uint32& wire_value)
{
    // 16 bits, only 13 are useable, so
    // 3 "blank bits" + 3 bits for us
    return goby::acomms::Bitset(MINI_ID_SIZE, wire_value - MINI_ID_OFFSET);
}


int main(int argc, char* argv[])
{
    goby::glog.add_stream(goby::common::logger::DEBUG3, &std::cerr);
    goby::glog.set_name(argv[0]);    
    
    goby::acomms::DCCLCodec* codec = goby::acomms::DCCLCodec::get();
    goby::acomms::protobuf::DCCLConfig cfg;
    cfg.set_crypto_passphrase("309ldskjfla39");
    codec->set_cfg(cfg);

    
    codec->add_id_codec<MicroModemMiniPacketDCCLIDCodec>("mini_id_codec");
    codec->set_id_codec("mini_id_codec");    

    codec->validate<MiniUser>();
    codec->info<MiniUser>(&goby::glog);    
    MiniUser mini_user_msg_in, mini_user_msg_out;
    mini_user_msg_in.set_user(876);
    std::string encoded;
    codec->encode(&encoded, mini_user_msg_in);
    codec->decode(encoded, &mini_user_msg_out);
    assert(mini_user_msg_out.SerializeAsString() == mini_user_msg_in.SerializeAsString());

    codec->validate<MiniOWTT>();
    codec->info<MiniOWTT>(&goby::glog);
        
    MiniOWTT mini_owtt_in, mini_owtt_out; 
    mini_owtt_in.set_clock_mode(3);
    mini_owtt_in.set_tod(12);
    mini_owtt_in.set_user(13);
        
    codec->encode(&encoded, mini_owtt_in);
    std::cout << "OWTT as hex: " << goby::util::hex_encode(encoded) << std::endl;
        
    codec->decode(encoded, &mini_owtt_out);
    assert(mini_owtt_out.SerializeAsString() == mini_owtt_in.SerializeAsString());

    codec->validate<MiniAbort>();
    codec->info<MiniAbort>(&goby::glog);

    MiniAbort mini_abort_in, mini_abort_out; 
    mini_abort_in.set_user(130);
    codec->encode(&encoded, mini_abort_in);
    codec->decode(encoded, &mini_abort_out);
    assert(mini_abort_out.SerializeAsString() == mini_abort_in.SerializeAsString());

    cfg.clear_crypto_passphrase();
    codec->set_cfg(cfg);

    void* dl_handle = dlopen("libdccl_ccl_compat" SHARED_LIBRARY_SUFFIX, RTLD_LAZY);

    if(!dl_handle)
    {
        std::cerr << "Failed to open libdccl_ccl_compat" SHARED_LIBRARY_SUFFIX << std::endl;
        exit(1);
    }
    codec->load_shared_library_codecs(dl_handle);
    codec->set_id_codec("dccl.ccl.id");
    
    codec->validate<NormalDCCL>();
    codec->info<NormalDCCL>(&goby::glog);
    NormalDCCL normal_msg, normal_msg_out;
    normal_msg.set_a(123);
    normal_msg.set_b(321);

    codec->encode(&encoded, normal_msg);
    std::cout << goby::util::hex_encode(encoded) << std::endl;
    assert(goby::util::hex_encode(encoded).substr(0, 2) == "20");
    codec->decode(encoded, &normal_msg_out);
    
    assert(normal_msg.SerializeAsString() == normal_msg_out.SerializeAsString());

    codec->info<dccl::legacyccl::protobuf::CCLMDATState>(&goby::glog);

    dccl::legacyccl::protobuf::CCLMDATState state_in, state_out;
    std::string test_state_encoded = "0e86fa11ad20c9011b4432bf47d10000002401042f0e7d87fa111620c95a200a";
    codec->decode(goby::util::hex_decode(test_state_encoded), &state_out);
    state_in.set_latitude(25.282416667);
    state_in.set_longitude(-77.164266667);
    state_in.set_fix_age(4);
    
    boost::posix_time::ptime time_date(
        boost::gregorian::date(boost::gregorian::day_clock::universal_day().year(),
                               boost::date_time::Mar, 04), 
        boost::posix_time::time_duration(17,1,44));

    state_in.set_time_date(goby::util::as<goby::uint64>(time_date));
    state_in.set_heading(270);
    state_in.set_depth(2323);
    state_in.set_mission_mode(dccl::legacyccl::protobuf::CCLMDATState::NORMAL);
    state_in.set_faults(goby::util::hex_decode("00000024"));
    state_in.set_faults_2(goby::util::hex_decode("01"));
    state_in.set_mission_leg(4);
    state_in.set_est_velocity(1.88);
    state_in.set_objective_index(goby::util::hex_decode("0E"));
    state_in.set_watts(500);
    state_in.set_lat_goal(25.282440815262891);
    state_in.set_lon_goal(-77.167505880296929);
    state_in.set_battery_percent(90);
    state_in.mutable_gfi_pitch_oil()->set_gfi(0);
    state_in.mutable_gfi_pitch_oil()->set_pitch(6);
    state_in.mutable_gfi_pitch_oil()->set_oil(55);
    

    assert(double_cmp(state_in.latitude(), state_out.latitude(), 4));
    assert(double_cmp(state_in.longitude(), state_out.longitude(), 4));
    assert(state_in.fix_age() == state_out.fix_age());
    assert(state_in.time_date() == state_out.time_date());
    assert(goby::util::unbiased_round(state_in.heading(),0) ==
           goby::util::unbiased_round(state_out.heading(),0));
    assert(double_cmp(state_in.depth(), state_out.depth(), 1));
    assert(state_in.mission_mode() == state_out.mission_mode());

    // test the dynamically generated message
    boost::shared_ptr<google::protobuf::Message> state_in_2 = goby::util::DynamicProtobufManager::new_protobuf_message(dccl::legacyccl::protobuf::CCLMDATState::descriptor());
    state_in_2->CopyFrom(state_in);
    
    std::string state_encoded;
    codec->encode(&state_encoded, *state_in_2);

    dccl::legacyccl::protobuf::CCLMDATState state_out_2;
    codec->decode(state_encoded, &state_out_2);

    std::cout << "in:" << state_in << std::endl;
    std::cout << test_state_encoded << std::endl;
    std::cout << goby::util::hex_encode(state_encoded) << std::endl;
    std::cout << std::setprecision(16) << state_out.lon_goal() << std::endl;
    std::cout << "out:" << state_out << std::endl;
    std::cout << "out2: " << state_out_2 << std::endl;

    assert(state_out.SerializeAsString() == state_out_2.SerializeAsString());
    assert(test_state_encoded == goby::util::hex_encode(state_encoded));
    
    std::cout << goby::util::hex_encode(state_out.faults()) << std::endl;
    std::cout << goby::util::hex_encode(state_out.faults_2()) << std::endl;


    codec->info<dccl::legacyccl::protobuf::CCLMDATRedirect>(&goby::glog);

    dccl::legacyccl::protobuf::CCLMDATRedirect redirect_in, redirect_out;
    std::string test_redirect_encoded = "07522cf9113d20c99964003d6464003d640be60014142035f911ef21c9000000";
    codec->decode(goby::util::hex_decode(test_redirect_encoded), &redirect_out);
    redirect_in.set_message_number(82);
    redirect_in.set_latitude(25.274995002149939);
    redirect_in.set_longitude(-77.166669030984522);
    redirect_in.set_transit_vertical_mode(dccl::legacyccl::protobuf::CCLMDATRedirect::ALTITUDE);  
    redirect_in.set_transit_thrust_mode(dccl::legacyccl::protobuf::CCLMDATRedirect::METERS_PER_SECOND);
    redirect_in.set_survey_vertical_mode(dccl::legacyccl::protobuf::CCLMDATRedirect::ALTITUDE);  
    redirect_in.set_survey_thrust_mode(dccl::legacyccl::protobuf::CCLMDATRedirect::METERS_PER_SECOND);

    redirect_in.set_depth_goal_transit(10.0);
    redirect_in.set_speed_transit(2.0333333);
    redirect_in.set_device_cmd_transit(100);

    redirect_in.set_depth_goal_survey(10.0);
    redirect_in.set_speed_survey(2.0333333);
    redirect_in.set_device_cmd_survey(100);

    redirect_in.set_num_rows(11);
    redirect_in.set_row_length(230);
    redirect_in.set_spacing_0(20);
    redirect_in.set_spacing_1(20);
    redirect_in.set_heading(45.176472);

    redirect_in.set_lat_start(25.275183333);
    redirect_in.set_lon_start(-77.15735);

    redirect_in.set_spare(std::string(3, '\0'));
    
    std::string redirect_encoded;

    codec->encode(&redirect_encoded, redirect_in);

    dccl::legacyccl::protobuf::CCLMDATRedirect redirect_out_2;
    codec->decode(redirect_encoded, &redirect_out_2);

    std::cout << "in:" << redirect_in << std::endl;
    std::cout << test_redirect_encoded << std::endl;
    std::cout << goby::util::hex_encode(redirect_encoded) << std::endl;
    std::cout << "out:" << redirect_out << std::endl;
    std::cout << "out2: " << redirect_out_2 << std::endl;

    assert(redirect_out.SerializeAsString() == redirect_out_2.SerializeAsString());
    assert(test_redirect_encoded == goby::util::hex_encode(redirect_encoded));
    

    codec->info<dccl::legacyccl::protobuf::CCLMDATEmpty>(&goby::glog);
    codec->info<dccl::legacyccl::protobuf::CCLMDATBathy>(&goby::glog);
    codec->info<dccl::legacyccl::protobuf::CCLMDATCTD>(&goby::glog);
    codec->info<dccl::legacyccl::protobuf::CCLMDATError>(&goby::glog);
    codec->info<dccl::legacyccl::protobuf::CCLMDATCommand>(&goby::glog);
    
    
    std::cout << "all tests passed" << std::endl;
}

