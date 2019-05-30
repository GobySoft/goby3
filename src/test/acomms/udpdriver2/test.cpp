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

// tests functionality of the UDPDriver

#include "goby/acomms/acomms_helpers.h"
#include "goby/acomms/connect.h"
#include "goby/acomms/modemdriver/udp_driver.h"
#include "goby/util/binary.h"
#include "goby/util/debug_logger.h"

using namespace goby::common::logger;
using namespace goby::acomms;
using goby::common::goby_time;
using goby::util::as;
using namespace boost::posix_time;

boost::asio::io_service io1;
std::shared_ptr<goby::acomms::UDPDriver> driver1;

void handle_data_request1(protobuf::ModemTransmission* msg);
void handle_modify_transmission1(protobuf::ModemTransmission* msg);
void handle_transmit_result1(const protobuf::ModemTransmission& msg);
void handle_data_receive1(const protobuf::ModemTransmission& msg);

int main(int argc, char* argv[])
{
    goby::glog.add_stream(goby::common::logger::DEBUG3, &std::clog);
    std::ofstream fout;

    if (argc < 6)
    {
        std::cerr << "Usage: test_udpdriver2 remote_addr remote_port remote_id local_port local_id "
                     "[is_quiet]"
                  << std::endl;
        exit(1);
    }

    std::string remote_addr = argv[1];
    int remote_port = as<int>(argv[2]);
    int remote_id = as<int>(argv[3]);
    int local_port = as<int>(argv[4]);
    int local_id = as<int>(argv[5]);

    bool is_quiet = false;
    if (argc == 7)
        is_quiet = as<bool>(argv[6]);

    goby::glog.set_name(argv[0]);

    driver1.reset(new goby::acomms::UDPDriver(&io1));

    goby::acomms::protobuf::DriverConfig cfg1;

    cfg1.set_modem_id(local_id);

    UDPDriverConfig::EndPoint* local_endpoint1 = cfg1.MutableExtension(UDPDriverConfig::local);

    local_endpoint1->set_port(local_port);

    UDPDriverConfig::EndPoint* remote_endpoint1 = cfg1.MutableExtension(UDPDriverConfig::remote);

    remote_endpoint1->set_ip(remote_addr);
    remote_endpoint1->set_port(remote_port);

    goby::acomms::connect(&driver1->signal_receive, &handle_data_receive1);
    goby::acomms::connect(&driver1->signal_transmit_result, &handle_transmit_result1);
    goby::acomms::connect(&driver1->signal_modify_transmission, &handle_modify_transmission1);
    goby::acomms::connect(&driver1->signal_data_request, &handle_data_request1);

    goby::glog << cfg1.DebugString() << std::endl;

    driver1->startup(cfg1);

    int i = 0;
    while (((i / 10) < 1))
    {
        driver1->do_work();

        usleep(100000);
        ++i;
    }

    goby::glog << group("test") << "Test 1" << std::endl;

    protobuf::ModemTransmission transmit;

    transmit.set_type(protobuf::ModemTransmission::DATA);
    transmit.set_src(local_id);
    transmit.set_dest(remote_id);
    transmit.set_rate(0);
    transmit.set_ack_requested(true);

    i = 0;
    for (;;)
    //    while(((i / 10) < 100))
    {
        driver1->do_work();

        if ((i % 100 == 0) && !is_quiet)
            driver1->handle_initiate_transmission(transmit);

        usleep(100000);
        ++i;
    }

    goby::glog << group("test") << "all tests passed" << std::endl;
}

void handle_data_request1(protobuf::ModemTransmission* msg)
{
    goby::glog << group("driver1") << "Data request: " << *msg << std::endl;

    static int i = 0;
    msg->add_frame(std::string(32, i++ % 256));

    goby::glog << group("driver1") << "Post data request: " << *msg << std::endl;
}

void handle_modify_transmission1(protobuf::ModemTransmission* msg)
{
    goby::glog << group("driver1") << "Can modify: " << *msg << std::endl;
}

void handle_transmit_result1(const protobuf::ModemTransmission& msg)
{
    goby::glog << group("driver1") << "Completed transmit: " << msg << std::endl;
}

void handle_data_receive1(const protobuf::ModemTransmission& msg)
{
    goby::glog << group("driver1") << "Received: " << msg << std::endl;
}
