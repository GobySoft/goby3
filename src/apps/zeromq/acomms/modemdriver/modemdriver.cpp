// Copyright 2024:
//   GobySoft, LLC (2013-)
//   Community contributors (see AUTHORS file)
// File authors:
//   Toby Schneider <toby@gobysoft.org>
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

#include <thread>

#include "goby/middleware/marshalling/protobuf.h"

#include "goby/acomms/connect.h"
#include "goby/acomms/modem_driver.h"
#include "goby/acomms/modemdriver/benthos_atm900_driver.h"
#include "goby/acomms/modemdriver/driver_exception.h"
#include "goby/acomms/modemdriver/iridium_driver.h"
#include "goby/acomms/modemdriver/iridium_shore_driver.h"
#include "goby/acomms/modemdriver/mm_driver.h"
#include "goby/acomms/modemdriver/popoto_driver.h"
#include "goby/acomms/modemdriver/store_server_driver.h"
#include "goby/acomms/modemdriver/udp_driver.h"
#include "goby/acomms/modemdriver/udp_multicast_driver.h"
#include "goby/acomms/protobuf/modem_driver_status.pb.h"
#include "goby/middleware/acomms/groups.h"
#include "goby/zeromq/application/single_thread.h"
#include "goby/zeromq/protobuf/modemdriver_config.pb.h"

using goby::glog;
using namespace goby::util::logger;
using goby::acomms::ModemDriverException;
using goby::acomms::protobuf::ModemTransmission;

namespace goby
{
namespace apps
{
namespace zeromq
{
namespace acomms
{
class ModemDriver : public goby::zeromq::SingleThreadApplication<protobuf::ModemDriverConfig>
{
  public:
    ModemDriver();
    ~ModemDriver();

  private:
    void loop();

    void handle_modem_data_request(ModemTransmission* msg);
    void handle_modem_receive(const ModemTransmission& message);

    void handle_data_response(const ModemTransmission& message);
    void handle_initiate_transmission(const ModemTransmission& message);

    void reset(const ModemDriverException& e);

    std::string modem_id_str() { return std::to_string(cfg().driver_cfg().modem_id()); }

  private:
    //    // for PBDriver
    //    std::unique_ptr<goby::common::ZeroMQService> zeromq_service_;

    std::unique_ptr<goby::acomms::ModemDriverBase> driver_;

    bool data_response_received_;
    ModemTransmission data_response_;

    bool initiate_transmit_pending_;
    ModemTransmission initiate_transmission_;

    bool driver_started_;

    double last_status_time_;
    goby::acomms::protobuf::ModemDriverStatus status_;

    goby::middleware::DynamicGroup tx_group_;
    goby::middleware::DynamicGroup rx_group_;

    goby::middleware::DynamicGroup data_request_group_;
    goby::middleware::DynamicGroup data_response_group_;

    goby::middleware::DynamicGroup status_group_;
};
} // namespace acomms
} // namespace zeromq
} // namespace apps
} // namespace goby

int main(int argc, char* argv[]) { goby::run<goby::apps::zeromq::acomms::ModemDriver>(argc, argv); }

goby::apps::zeromq::acomms::ModemDriver::ModemDriver()
    : goby::zeromq::SingleThreadApplication<protobuf::ModemDriverConfig>(10 *
                                                                         boost::units::si::hertz),
      data_response_received_(false),
      initiate_transmit_pending_(false),
      driver_started_(false),
      last_status_time_(0),
      tx_group_(goby::middleware::acomms::groups::tx, cfg().driver_cfg().modem_id()),
      rx_group_(goby::middleware::acomms::groups::rx, cfg().driver_cfg().modem_id()),
      data_request_group_(goby::middleware::acomms::groups::data_request,
                          cfg().driver_cfg().modem_id()),
      data_response_group_(goby::middleware::acomms::groups::data_response,
                           cfg().driver_cfg().modem_id()),
      status_group_(goby::middleware::acomms::groups::status, cfg().driver_cfg().modem_id())
{
    switch (cfg().driver_cfg().driver_type())
    {
        case goby::acomms::protobuf::DRIVER_WHOI_MICROMODEM:
            driver_ = std::make_unique<goby::acomms::MMDriver>();
            break;

        case goby::acomms::protobuf::DRIVER_IRIDIUM:
            driver_ = std::make_unique<goby::acomms::IridiumDriver>();
            break;

        case goby::acomms::protobuf::DRIVER_UDP:
            driver_ = std::make_unique<goby::acomms::UDPDriver>();
            break;

        case goby::acomms::protobuf::DRIVER_UDP_MULTICAST:
            driver_ = std::make_unique<goby::acomms::UDPMulticastDriver>();
            break;

        case goby::acomms::protobuf::DRIVER_IRIDIUM_SHORE:
            driver_ = std::make_unique<goby::acomms::IridiumShoreDriver>();
            break;

        case goby::acomms::protobuf::DRIVER_BENTHOS_ATM900:
            driver_ = std::make_unique<goby::acomms::BenthosATM900Driver>();
            break;

        case goby::acomms::protobuf::DRIVER_POPOTO:
            driver_ = std::make_unique<goby::acomms::PopotoDriver>();
            break;

        case goby::acomms::protobuf::DRIVER_STORE_SERVER:
            driver_ = std::make_unique<goby::acomms::StoreServerDriver>();
            break;

        default:
        case goby::acomms::protobuf::DRIVER_NONE:
            throw(goby::Exception("Invalid/unsupported driver specified"));
            break;
    }

    interprocess().subscribe_dynamic<ModemTransmission>(
        std::bind(&ModemDriver::handle_initiate_transmission, this, std::placeholders::_1),
        tx_group_);
    interprocess().subscribe_dynamic<ModemTransmission>(
        std::bind(&ModemDriver::handle_data_response, this, std::placeholders::_1),
        data_response_group_);

    goby::acomms::connect(&driver_->signal_receive, this, &ModemDriver::handle_modem_receive);

    goby::acomms::connect(&driver_->signal_data_request, this,
                          &ModemDriver::handle_modem_data_request);

    status_.set_src(cfg().driver_cfg().modem_id());
    status_.set_status(goby::acomms::protobuf::ModemDriverStatus::NOMINAL);
}

goby::apps::zeromq::acomms::ModemDriver::~ModemDriver()
{
    if (driver_)
        driver_->shutdown();
}

void goby::apps::zeromq::acomms::ModemDriver::loop()
{
    if (driver_)
    {
        try
        {
            if (!driver_started_)
            {
                driver_->startup(cfg().driver_cfg());
                driver_started_ = true;
                status_.set_status(goby::acomms::protobuf::ModemDriverStatus::NOMINAL);
            }
            driver_->do_work();
        }
        catch (const ModemDriverException& e)
        {
            reset(e);
        }
    }

    double now = goby::time::SystemClock::now<goby::time::SITime>().value();
    if (last_status_time_ + cfg().status_period_s() <= now)
    {
        status_.set_time(now);
        interprocess().publish_dynamic(status_, status_group_);
        last_status_time_ = now;
    }

    if (initiate_transmit_pending_)
    {
        driver_->handle_initiate_transmission(initiate_transmission_);
        initiate_transmit_pending_ = false;
    }
}

void goby::apps::zeromq::acomms::ModemDriver::handle_modem_data_request(ModemTransmission* msg)
{
    interprocess().publish_dynamic(*msg, data_request_group_);
    data_response_received_ = false;

    double start_time = goby::time::SystemClock::now<goby::time::SITime>().value();
    while (goby::time::SystemClock::now<goby::time::SITime>().value() <
           start_time + cfg().data_request_timeout())
    {
        interprocess().poll(std::chrono::milliseconds(10));
        if (data_response_received_)
        {
            *msg = data_response_;
            break;
        }
    }
    if (!data_response_received_)
        glog.is(WARN) && glog << "Timeout waiting for response to data request" << std::endl;
}

void goby::apps::zeromq::acomms::ModemDriver::handle_modem_receive(const ModemTransmission& message)
{
    interprocess().publish_dynamic(message, rx_group_);
}

void goby::apps::zeromq::acomms::ModemDriver::handle_data_response(const ModemTransmission& message)
{
    data_response_received_ = true;
    data_response_ = message;
}

void goby::apps::zeromq::acomms::ModemDriver::handle_initiate_transmission(
    const ModemTransmission& message)
{
    // wait until we enter next loop to initiate the transmission to avoid calling poll() from within poll()
    initiate_transmit_pending_ = true;
    initiate_transmission_ = message;
}

void goby::apps::zeromq::acomms::ModemDriver::reset(const ModemDriverException& e)
{
    status_.set_status(e.status());
    status_.set_n_resets(status_.n_resets() + 1);
    glog.is(WARN) && glog << "Exception: " << e.what() << std::endl;
    const int restart_sec = 15;
    glog.is(WARN) && glog << "Shutting down driver." << std::endl;
    driver_->shutdown();
    glog.is(WARN) && glog << "Attempting to restart driver in " << restart_sec << " seconds."
                          << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(restart_sec));
    driver_started_ = false;
}
