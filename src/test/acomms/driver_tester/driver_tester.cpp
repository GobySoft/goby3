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

#include "driver_tester.h"


using namespace goby::common::logger;
using namespace goby::acomms;
using goby::util::as;
using goby::common::goby_time;
using namespace boost::posix_time;

DriverTester::DriverTester(boost::shared_ptr<goby::acomms::ModemDriverBase> driver1,
                           boost::shared_ptr<goby::acomms::ModemDriverBase> driver2,
                           const goby::acomms::protobuf::DriverConfig& cfg1,
                           const goby::acomms::protobuf::DriverConfig& cfg2,
                           const std::vector<int>& tests_to_run, 
                           goby::acomms::protobuf::DriverType driver_type)
    
    :  driver1_(driver1),
       driver2_(driver2),
    check_count_(0),
    tests_to_run_(tests_to_run),
    tests_to_run_index_(0),
    test_number_(-1),
    driver_type_(driver_type)
{
            
    goby::glog.add_group("test", goby::common::Colors::green);
    goby::glog.add_group("driver1", goby::common::Colors::green);
    goby::glog.add_group("driver2", goby::common::Colors::yellow);
    
    goby::acomms::connect(&driver1_->signal_receive, this, &DriverTester::handle_data_receive1);
    goby::acomms::connect(&driver1_->signal_transmit_result, this, &DriverTester::handle_transmit_result1);
    goby::acomms::connect(&driver1_->signal_modify_transmission, this, &DriverTester::handle_modify_transmission1);
    goby::acomms::connect(&driver1_->signal_data_request, this, &DriverTester::handle_data_request1);
    goby::acomms::connect(&driver2_->signal_receive, this, &DriverTester::handle_data_receive2);
    goby::acomms::connect(&driver2_->signal_transmit_result, this, &DriverTester::handle_transmit_result2);
    goby::acomms::connect(&driver2_->signal_modify_transmission, this, &DriverTester::handle_modify_transmission2);
    goby::acomms::connect(&driver2_->signal_data_request, this, &DriverTester::handle_data_request2);

    goby::glog << cfg1.DebugString() << std::endl;
    goby::glog << cfg2.DebugString() << std::endl;

    driver1_->startup(cfg1);
    driver2_->startup(cfg2);

    
    int i = 0;
    while(((i / 10) < 3))
    {
        driver1_->do_work();
        driver2_->do_work();
        
        usleep(100000);
        ++i;
    }

    test_str0_.resize(32);
    for(std::string::size_type i = 0, n = test_str0_.size(); i < n; ++i)
        test_str0_[i] = i;

    test_str1_.resize(64);
    for(std::string::size_type i = 0, n = test_str1_.size(); i < n; ++i)
        test_str1_[i] = i + 64; // skip by some of low bits
    
    test_str2_.resize(64);
    for(std::string::size_type i = 0, n = test_str2_.size(); i < n; ++i)
        test_str2_[i] = i + 2*64;

    test_str3_.resize(64);
    for(std::string::size_type i = 0, n = test_str3_.size(); i < n; ++i)
        test_str3_[i] = i + 3*64;

    test_number_=tests_to_run_[tests_to_run_index_];
}


int DriverTester::run()
{

    try
    {    
        
        for(;;)
        {
            switch(test_number_)
            {
                case 0: test0(); break; 
                case 1: test1(); break; 
                case 2: test2(); break; 
                case 3: test3(); break; 
                case 4: test4(); break; 
                case 5: test5(); break;
                case 6: test6(); break;
                case -1:
                    goby::glog << group("test") << "all tests passed" << std::endl;
                    driver1_->shutdown();
                    driver2_->shutdown();
                    
                    return 0;
            }    
            
            goby::glog << "Test " << group("test") << test_number_ << " passed." << std::endl;
            ++tests_to_run_index_;

            if(tests_to_run_index_ < static_cast<int>(tests_to_run_.size()))
                test_number_ = tests_to_run_[tests_to_run_index_];
            else
                test_number_ = -1;            
            
            check_count_ = 0;
            sleep(1);
            
        }
    }
    catch(std::exception& e)
    {
        goby::glog <<  warn << "Exception: " << e.what() << std::endl;
        sleep(5);
        exit(2);
    }

}




void DriverTester::handle_data_request1(protobuf::ModemTransmission* msg)
{
    goby::glog << group("driver1") << "Data request: " << *msg << std::endl;

    switch(test_number_)
    {
        case 4:
        {
            msg->add_frame(test_str0_);
            static bool entered = false;
            if(!entered)
            {
                ++check_count_;
                entered = true;
            }            
        }
        break;
            
        case 5:
        {   
            static bool entered = false;
            msg->add_frame(test_str2_);
            msg->add_frame(test_str3_);
            if(!entered)
            {
                ++check_count_;
                entered = true;
            }            
        }
        break;
            
    }

    goby::glog << group("driver1") << "Post data request: " << *msg << std::endl;
}

void DriverTester::handle_modify_transmission1(protobuf::ModemTransmission* msg)
{
    goby::glog << group("driver1") << "Can modify: " << *msg << std::endl;
}

void DriverTester::handle_transmit_result1(const protobuf::ModemTransmission& msg)
{
    goby::glog << group("driver1") << "Completed transmit: " << msg << std::endl;
}


void DriverTester::handle_data_receive1(const protobuf::ModemTransmission& msg)
{
    goby::glog << group("driver1") << "Received: " << msg << std::endl;

    switch(test_number_)
    {
        case 0:
        {
            if(driver_type_ == goby::acomms::protobuf::DRIVER_WHOI_MICROMODEM)
            {
                assert(msg.type() == protobuf::ModemTransmission::DRIVER_SPECIFIC &&
                       msg.GetExtension(micromodem::protobuf::type) == micromodem::protobuf::MICROMODEM_TWO_WAY_PING);
                ++check_count_;
            }
            else if(driver_type_ == goby::acomms::protobuf::DRIVER_BENTHOS_ATM900)
            {
                assert(msg.type() == protobuf::ModemTransmission::DRIVER_SPECIFIC &&
                       msg.GetExtension(benthos::protobuf::type) == benthos::protobuf::BENTHOS_TWO_WAY_PING);
                ++check_count_;
            }
            break;
        }
        
        case 1:
        {
            assert(msg.type() == protobuf::ModemTransmission::DRIVER_SPECIFIC &&
                   msg.GetExtension(micromodem::protobuf::type) == micromodem::protobuf::MICROMODEM_REMUS_LBL_RANGING);

            assert(msg.src() == 1);
            assert(!msg.has_dest());

            ptime now = goby_time();
            ptime reported = as<ptime>(msg.time());                
            assert(reported < now && reported > now - seconds(2));
            ++check_count_;
        }
        break;

        case 2:
        {
            assert(msg.type() == protobuf::ModemTransmission::DRIVER_SPECIFIC &&
                   msg.GetExtension(micromodem::protobuf::type) == micromodem::protobuf::MICROMODEM_NARROWBAND_LBL_RANGING);

            assert(msg.src() == 1);
            assert(!msg.has_dest());

            ptime now = goby_time();
            ptime reported = as<ptime>(msg.time());                
            assert(reported < now && reported > now - seconds(2));
            ++check_count_;
        }
        break;

        case 3:
        {
            assert(msg.type() == protobuf::ModemTransmission::DRIVER_SPECIFIC &&
                   msg.GetExtension(micromodem::protobuf::type) == micromodem::protobuf::MICROMODEM_MINI_DATA);

            assert(msg.src() == 2);
            assert(msg.dest() == 1);
            assert(msg.frame_size() == 1);
            assert(msg.frame(0) == goby::util::hex_decode("0123"));
            ++check_count_;
        }
        break;
            
        case 4:
        {
            assert(msg.type() == protobuf::ModemTransmission::ACK);
            assert(msg.src() == 2);
            assert(msg.dest() == 1);
            assert(msg.acked_frame_size() == 1 && msg.acked_frame(0) == 0);
            ++check_count_;
        }
        break;

        case 5:
        {
            assert(msg.type() == protobuf::ModemTransmission::ACK);
            assert(msg.src() == 2);
            assert(msg.dest() == 1);
            assert(msg.acked_frame_size() == 3 && msg.acked_frame(1) ==  msg.acked_frame(0) + 1 && msg.acked_frame(2) ==  msg.acked_frame(0) + 2);
            ++check_count_;
        }
        break;
        
        case 6:
        {
            assert(msg.type() == protobuf::ModemTransmission::DRIVER_SPECIFIC &&
                   msg.GetExtension(micromodem::protobuf::type) == micromodem::protobuf::MICROMODEM_FLEXIBLE_DATA);

            assert(msg.src() == 2);
            assert(msg.dest() == 1);
            assert(msg.rate() == 1);
            assert(msg.frame_size() == 1);

            std::cout << "[" << goby::util::hex_encode(msg.frame(0)) << "]" << std::endl;
            assert(msg.frame(0) == goby::util::hex_decode("00112233445566778899001122334455667788990011"));
            ++check_count_;
        }
        break;


        default:
            break;
    }
}


void DriverTester::handle_data_request2(protobuf::ModemTransmission* msg)
{
    goby::glog << group("driver2") << "Data request: " << *msg << std::endl;

    switch(test_number_)
    {
        default:
            assert(false);
            break;

        case 3:
        {
            static bool entered = false;
            if(!entered)
            {
                ++check_count_;
                entered = true;
            }
                             

            msg->add_frame(goby::util::hex_decode("0123"));
            break;
        }
        
        case 4:
            break;

        case 6:
        {
            static bool entered = false;
            if(!entered)
            {
                ++check_count_;
                entered = true;
            }
                             

            msg->add_frame(goby::util::hex_decode("00112233445566778899001122334455667788990011"));
            break;
        }

            
    }
    
    goby::glog << group("driver2") << "Post data request: " << *msg << std::endl;
}

void DriverTester::handle_modify_transmission2(protobuf::ModemTransmission* msg)
{
    goby::glog << group("driver2") << "Can modify: " << *msg << std::endl;
}

void DriverTester::handle_transmit_result2(const protobuf::ModemTransmission& msg)
{
    goby::glog << group("driver2") << "Completed transmit: " << msg << std::endl;
}

void DriverTester::handle_data_receive2(const protobuf::ModemTransmission& msg)
{
    goby::glog << group("driver2") << "Received: " << msg << std::endl;

    switch(test_number_)
    {
        default:
            break;

        case 0:
            if(driver_type_ == goby::acomms::protobuf::DRIVER_WHOI_MICROMODEM)
            {
                assert(msg.type() == protobuf::ModemTransmission::DRIVER_SPECIFIC &&
                       msg.GetExtension(micromodem::protobuf::type) == micromodem::protobuf::MICROMODEM_TWO_WAY_PING);
                ++check_count_;
            }
            break;

        case 4:
        {
            if(msg.type() == protobuf::ModemTransmission::DATA)
            {
                assert(msg.src() == 1);
                assert(msg.dest() == 2);
                assert(msg.frame_size() == 1);
                assert(msg.frame(0) == test_str0_);
                ++check_count_;
            }

            break;
        }
        
        case 5:
            if(msg.type() == protobuf::ModemTransmission::DATA)
            {
                assert(msg.src() == 1);
                assert(msg.dest() == 2);
                assert(msg.frame_size() == 3);
                assert(msg.frame(0) == test_str1_);
                assert(msg.frame(1) == test_str2_);
                assert(msg.frame(2) == test_str3_);
                ++check_count_;
            }
            break;

            
    }

}


void DriverTester::test0()
{
    // ping test
    goby::glog << group("test") << "Ping test" << std::endl;

    protobuf::ModemTransmission transmit;
    
    transmit.set_type(protobuf::ModemTransmission::DRIVER_SPECIFIC);

    if(driver_type_ == goby::acomms::protobuf::DRIVER_WHOI_MICROMODEM)
        transmit.SetExtension(micromodem::protobuf::type, micromodem::protobuf::MICROMODEM_TWO_WAY_PING);
    else if(driver_type_ == goby::acomms::protobuf::DRIVER_BENTHOS_ATM900)
        transmit.SetExtension(benthos::protobuf::type, benthos::protobuf::BENTHOS_TWO_WAY_PING);
    
    transmit.set_src(1);
    transmit.set_dest(2);

    driver1_->handle_initiate_transmission(transmit);

    int i = 0;
    while(((i / 10) < 10) && check_count_ < 2)
    {
        driver1_->do_work();
        driver2_->do_work();
        
        usleep(100000);
        ++i;
    }

    if(driver_type_ == goby::acomms::protobuf::DRIVER_WHOI_MICROMODEM)
        assert(check_count_ == 2);
    else if(driver_type_ == goby::acomms::protobuf::DRIVER_BENTHOS_ATM900)
        assert(check_count_ == 1); // no clear indication of ping on the pinged modem
}


void DriverTester::test1()
{
    goby::glog << group("test") << "Remus LBL test" << std::endl;

    protobuf::ModemTransmission transmit;
    
    transmit.set_type(protobuf::ModemTransmission::DRIVER_SPECIFIC);
    transmit.SetExtension(micromodem::protobuf::type, micromodem::protobuf::MICROMODEM_REMUS_LBL_RANGING);

    transmit.set_src(1);
    transmit.MutableExtension(micromodem::protobuf::remus_lbl)->set_lbl_max_range(1000);
    
    driver1_->handle_initiate_transmission(transmit);

    int i = 0;
    while(((i / 10) < 10) && check_count_ < 1)
    {
        driver1_->do_work();
        driver2_->do_work();
        
        usleep(100000);
        ++i;
    }
    assert(check_count_ == 1);
}

void DriverTester::test2()
{
    goby::glog << group("test") << "Narrowband LBL test" << std::endl;

    protobuf::ModemTransmission transmit;
    
    transmit.set_type(protobuf::ModemTransmission::DRIVER_SPECIFIC);
    transmit.SetExtension(micromodem::protobuf::type, micromodem::protobuf::MICROMODEM_NARROWBAND_LBL_RANGING);
    transmit.set_src(1);

    micromodem::protobuf::NarrowBandLBLParams* params = transmit.MutableExtension(micromodem::protobuf::narrowband_lbl);
    params->set_lbl_max_range(1000);
    params->set_turnaround_ms(20);
    params->set_transmit_freq(26000);
    params->set_transmit_ping_ms(5);
    params->set_receive_ping_ms(5);
    params->add_receive_freq(25000);
    params->set_transmit_flag(true);
    
    driver1_->handle_initiate_transmission(transmit);

    int i = 0;
    while(((i / 10) < 10) && check_count_ < 1)
    {
        driver1_->do_work();
        driver2_->do_work();
        
        usleep(100000);
        ++i;
    }
    assert(check_count_ == 1);
}

void DriverTester::test3()
{
    goby::glog << group("test") << "Mini data test" << std::endl;

    protobuf::ModemTransmission transmit;
    
    transmit.set_type(protobuf::ModemTransmission::DRIVER_SPECIFIC);
    transmit.SetExtension(micromodem::protobuf::type, micromodem::protobuf::MICROMODEM_MINI_DATA);

    transmit.set_src(2);
    transmit.set_dest(1);
    
    driver2_->handle_initiate_transmission(transmit);

    int i = 0;
    while(((i / 10) < 10) && check_count_ < 2)
    {
        driver1_->do_work();
        driver2_->do_work();
        
        usleep(100000);
        ++i;
    }
    assert(check_count_ == 2);
}


void DriverTester::test4()
{
    goby::glog << group("test") << "Rate 0 test" << std::endl;
    
    protobuf::ModemTransmission transmit;
    
    transmit.set_type(protobuf::ModemTransmission::DATA);
    transmit.set_src(1);
    transmit.set_dest(2);
    transmit.set_rate(0);
    transmit.set_ack_requested(true);
//    transmit.set_ack_requested(false);
    driver1_->handle_initiate_transmission(transmit);

    
    int i = 0;
    while(((i / 10) < 60) && check_count_ < 3)
    {
        driver1_->do_work();
        driver2_->do_work();
        
        usleep(100000);
        ++i;
    }
    assert(check_count_ == 3);
}

void DriverTester::test5()
{
    goby::glog << group("test") << "Rate 2 test" << std::endl;
    
    protobuf::ModemTransmission transmit;
    
    transmit.set_type(protobuf::ModemTransmission::DATA);
    transmit.set_src(1);
    transmit.set_dest(2);
    transmit.set_rate(2);

    transmit.add_frame(test_str1_);
    transmit.set_ack_requested(true);
    
    driver1_->handle_initiate_transmission(transmit);

    int i = 0;
    while(((i / 10) < 60) && check_count_ < 3)
    {
        driver1_->do_work();
        driver2_->do_work();
        
        usleep(100000);
        ++i;
    }
    assert(check_count_ == 3);
}


void DriverTester::test6()
{
    goby::glog << group("test") << "FDP data test" << std::endl;

    protobuf::ModemTransmission transmit;
    
    transmit.set_type(protobuf::ModemTransmission::DRIVER_SPECIFIC);
    transmit.SetExtension(micromodem::protobuf::type, micromodem::protobuf::MICROMODEM_FLEXIBLE_DATA);

    dynamic_cast<goby::acomms::MMDriver*>(driver1_.get())->write_single_cfg("psk.packet.mod_hdr_version,1");
    dynamic_cast<goby::acomms::MMDriver*>(driver2_.get())->write_single_cfg("psk.packet.mod_hdr_version,1");

    
    transmit.set_src(2);
    transmit.set_dest(1);
    transmit.set_rate(1);
    
    driver2_->handle_initiate_transmission(transmit);

    int i = 0;
    while(((i / 10) < 10) && check_count_ < 2)
    {
        driver1_->do_work();
        driver2_->do_work();
        
        usleep(100000);
        ++i;
    }
    assert(check_count_ == 2);
}
