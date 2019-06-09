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

// tests functionality of the MMDriver WHOI Micro-Modem driver

#include "goby/acomms/connect.h"
#include "goby/acomms/modemdriver/mm_driver.h"
#include "goby/middleware/application.h"
#include "goby/util/binary.h"
#include "goby/util/debug_logger.h"
#include "goby/util/protobuf/io.h"

#include "test_config.pb.h"
using namespace goby::acomms;
using namespace goby::util::logger;
using namespace goby::util::logger_lock;
using goby::util::as;
using namespace boost::posix_time;

std::mutex driver_mutex;
int last_transmission_index = 0;

class MMDriverTest2
    : public goby::middleware::Application<goby::test::protobuf::MMDriverTest2Config>
{
  public:
    MMDriverTest2();

  private:
    void run() override;

    void run_driver(goby::acomms::MMDriver& modem, goby::acomms::protobuf::DriverConfig& cfg);

    void handle_transmit_result1(const protobuf::ModemTransmission& msg);
    void handle_data_receive1(const protobuf::ModemTransmission& msg);

    void handle_transmit_result2(const protobuf::ModemTransmission& msg);
    void handle_data_receive2(const protobuf::ModemTransmission& msg);

    void
    summary(const std::map<int, std::vector<micromodem::protobuf::ReceiveStatistics> >& receive,
            const goby::acomms::protobuf::DriverConfig& cfg);

  private:

    goby::acomms::MMDriver driver1, driver2;
    // maps transmit index to receive statistics
    std::map<int, std::vector<micromodem::protobuf::ReceiveStatistics> > driver1_receive,
        driver2_receive;

    bool modems_running_;
};

MMDriverTest2::MMDriverTest2() : modems_running_(true)
{
    goby::glog.set_lock_action(lock);

    goby::glog.is(VERBOSE) && goby::glog << "Running test: " << app_cfg() << std::endl;
}

void MMDriverTest2::run()
{
    goby::acomms::connect(&driver1.signal_receive, this, &MMDriverTest2::handle_data_receive1);
    goby::acomms::connect(&driver1.signal_transmit_result, this,
                          &MMDriverTest2::handle_transmit_result1);
    goby::acomms::connect(&driver2.signal_receive, this, &MMDriverTest2::handle_data_receive2);
    goby::acomms::connect(&driver2.signal_transmit_result, this,
                          &MMDriverTest2::handle_transmit_result2);

    std::thread modem_thread_A(
        boost::bind(&MMDriverTest2::run_driver, this, boost::ref(driver1), app_cfg().mm1_cfg()));
    std::thread modem_thread_B(
        boost::bind(&MMDriverTest2::run_driver, this, boost::ref(driver2), app_cfg().mm2_cfg()));

    while (!driver1.is_started() || !driver2.is_started()) usleep(10000);

    for (int i = 0, n = app_cfg().repeat(); i < n; ++i)
    {
        goby::glog.is(VERBOSE) && goby::glog << "Beginning test sequence repetition " << i + 1
                                             << " of " << n << std::endl;

        int o = app_cfg().transmission_size();
        for (last_transmission_index = 0; last_transmission_index < o; ++last_transmission_index)
        {
            if (app_cfg().transmission(last_transmission_index).src() == 1)
            {
                std::lock_guard<std::mutex> lock(driver_mutex);
                driver1.handle_initiate_transmission(
                    app_cfg().transmission(last_transmission_index));
            }
            else if (app_cfg().transmission(last_transmission_index).src() == 2)
            {
                std::lock_guard<std::mutex> lock(driver_mutex);
                driver2.handle_initiate_transmission(
                    app_cfg().transmission(last_transmission_index));
            }
            sleep(app_cfg().transmission(last_transmission_index).slot_seconds());
        }
    }

    modems_running_ = false;
    modem_thread_A.join();
    modem_thread_B.join();

    summary(driver1_receive, app_cfg().mm1_cfg());
    summary(driver2_receive, app_cfg().mm2_cfg());

    goby::glog.is(VERBOSE) && goby::glog << "Completed test" << std::endl;
    quit();
}

void MMDriverTest2::run_driver(goby::acomms::MMDriver& modem,
                               goby::acomms::protobuf::DriverConfig& cfg)
{
    goby::glog.is(VERBOSE) && goby::glog << "Initializing modem" << std::endl;
    modem.startup(cfg);

    while (modems_running_)
    {
        {
            std::lock_guard<std::mutex> lock(driver_mutex);
            modem.do_work();
        }
        usleep(100000); // 0.1 seconds
    }

    modem.shutdown();
}

void MMDriverTest2::handle_data_receive1(const protobuf::ModemTransmission& msg)
{
    goby::glog.is(VERBOSE) && goby::glog << "modem 1 Received: " << msg << std::endl;

    for (int i = 0, n = msg.ExtensionSize(micromodem::protobuf::receive_stat); i < n; ++i)
        driver1_receive[last_transmission_index].push_back(
            msg.GetExtension(micromodem::protobuf::receive_stat, i));
}

void MMDriverTest2::handle_data_receive2(const protobuf::ModemTransmission& msg)
{
    goby::glog.is(VERBOSE) && goby::glog << "modem 2 Received: " << msg << std::endl;

    for (int i = 0, n = msg.ExtensionSize(micromodem::protobuf::receive_stat); i < n; ++i)
        driver2_receive[last_transmission_index].push_back(
            msg.GetExtension(micromodem::protobuf::receive_stat, i));
}

void MMDriverTest2::handle_transmit_result1(const protobuf::ModemTransmission& msg)
{
    goby::glog.is(VERBOSE) && goby::glog << "modem 1 Transmitted: " << msg << std::endl;
}

void MMDriverTest2::handle_transmit_result2(const protobuf::ModemTransmission& msg)
{
    goby::glog.is(VERBOSE) && goby::glog << "modem 2 Transmitted: " << msg << std::endl;
}

void MMDriverTest2::summary(
    const std::map<int, std::vector<micromodem::protobuf::ReceiveStatistics> >& receive,
    const goby::acomms::protobuf::DriverConfig& cfg)
{
    goby::glog.is(VERBOSE) && goby::glog << "*** Begin modem " << cfg.modem_id()
                                         << " receive summary" << std::endl;

    for (std::map<int, std::vector<micromodem::protobuf::ReceiveStatistics> >::const_iterator
             it = receive.begin(),
             end = receive.end();
         it != end; ++it)
    {
        goby::glog.is(VERBOSE) && goby::glog << "** Showing stats for this transmission (last "
                                                "transmission before this reception occured): "
                                             << app_cfg().transmission(it->first).DebugString()
                                             << std::flush;

        const std::vector<micromodem::protobuf::ReceiveStatistics>& current_receive_vector =
            it->second;

        std::multiset<micromodem::protobuf::PacketType> type;
        std::multiset<micromodem::protobuf::ReceiveMode> mode;
        std::multiset<micromodem::protobuf::PSKErrorCode> code;

        for (int i = 0, n = current_receive_vector.size(); i < n; ++i)
        {
            goby::glog.is(VERBOSE) && goby::glog << "CACST #" << i << ": "
                                                 << current_receive_vector[i].ShortDebugString()
                                                 << std::endl;

            type.insert(current_receive_vector[i].packet_type());
            mode.insert(current_receive_vector[i].mode());
            code.insert(current_receive_vector[i].psk_error_code());
        }

        goby::glog.is(VERBOSE) && goby::glog << "PacketType: " << std::endl;
        for (int j = micromodem::protobuf::PacketType_MIN;
             j <= micromodem::protobuf::PacketType_MAX; ++j)
        {
            if (micromodem::protobuf::PacketType_IsValid(j))
            {
                micromodem::protobuf::PacketType jt =
                    static_cast<micromodem::protobuf::PacketType>(j);
                goby::glog.is(VERBOSE) && goby::glog << "\t"
                                                     << micromodem::protobuf::PacketType_Name(jt)
                                                     << ": " << type.count(jt) << std::endl;
            }
        }

        goby::glog.is(VERBOSE) && goby::glog << "ReceiveMode: " << std::endl;
        for (int j = micromodem::protobuf::ReceiveMode_MIN;
             j <= micromodem::protobuf::ReceiveMode_MAX; ++j)
        {
            if (micromodem::protobuf::ReceiveMode_IsValid(j))
            {
                micromodem::protobuf::ReceiveMode jt =
                    static_cast<micromodem::protobuf::ReceiveMode>(j);
                goby::glog.is(VERBOSE) && goby::glog << "\t"
                                                     << micromodem::protobuf::ReceiveMode_Name(jt)
                                                     << ": " << mode.count(jt) << std::endl;
            }
        }

        goby::glog.is(VERBOSE) && goby::glog << "PSKErrorCode: " << std::endl;
        for (int j = micromodem::protobuf::PSKErrorCode_MIN;
             j <= micromodem::protobuf::PSKErrorCode_MAX; ++j)
        {
            if (micromodem::protobuf::PSKErrorCode_IsValid(j))
            {
                micromodem::protobuf::PSKErrorCode jt =
                    static_cast<micromodem::protobuf::PSKErrorCode>(j);
                goby::glog.is(VERBOSE) && goby::glog << "\t"
                                                     << micromodem::protobuf::PSKErrorCode_Name(jt)
                                                     << ": " << code.count(jt) << std::endl;
            }
        }
    }
    goby::glog.is(VERBOSE) && goby::glog << "*** End modem " << cfg.modem_id() << " receive summary"
                                         << std::endl;
}

int main(int argc, char* argv[]) { goby::run<MMDriverTest2>(argc, argv); }
