// Copyright 2009-2018 Toby Schneider (http://gobysoft.org/index.wt/people/toby)
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

#include "goby/acomms/acomms_helpers.h"
#include "goby/acomms/connect.h"
#include "goby/acomms/modemdriver/mm_driver.h"
#include "goby/acomms/protobuf/benthos_atm900.pb.h"
#include "goby/common/logger.h"
#include "goby/util/binary.h"

class DriverTester
{
  public:
    DriverTester(boost::shared_ptr<goby::acomms::ModemDriverBase> driver1,
                 boost::shared_ptr<goby::acomms::ModemDriverBase> driver2,
                 const goby::acomms::protobuf::DriverConfig& cfg1,
                 const goby::acomms::protobuf::DriverConfig& cfg2,
                 const std::vector<int>& tests_to_run,
                 goby::acomms::protobuf::DriverType driver_type);

    int run();

  private:
    void handle_data_request1(goby::acomms::protobuf::ModemTransmission* msg);
    void handle_modify_transmission1(goby::acomms::protobuf::ModemTransmission* msg);
    void handle_transmit_result1(const goby::acomms::protobuf::ModemTransmission& msg);
    void handle_data_receive1(const goby::acomms::protobuf::ModemTransmission& msg);

    void handle_data_request2(goby::acomms::protobuf::ModemTransmission* msg);
    void handle_modify_transmission2(goby::acomms::protobuf::ModemTransmission* msg);
    void handle_transmit_result2(const goby::acomms::protobuf::ModemTransmission& msg);
    void handle_data_receive2(const goby::acomms::protobuf::ModemTransmission& msg);

    void test0();
    void test1();
    void test2();
    void test3();
    void test4();
    void test5();
    void test6();

  private:
    boost::shared_ptr<goby::acomms::ModemDriverBase> driver1_, driver2_;

    int check_count_;

    std::vector<int> tests_to_run_;
    int tests_to_run_index_;
    int test_number_;

    std::string test_str0_, test_str1_, test_str2_, test_str3_;
    goby::acomms::protobuf::DriverType driver_type_;
};
