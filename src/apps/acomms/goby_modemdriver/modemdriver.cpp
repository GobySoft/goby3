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

#include "goby/common/logger.h"
#include "goby/common/logger/term_color.h"
#include "goby/common/zeromq_service.h"

#include "goby/acomms/connect.h"
#include "goby/acomms/modem_driver.h"
#include "goby/acomms/modemdriver/driver_exception.h"
#include "goby/acomms/modemdriver/iridium_driver.h"
#include "goby/acomms/modemdriver/iridium_shore_driver.h"
#include "goby/acomms/modemdriver/udp_driver.h"

#include "goby/pb/application.h"
#include "goby/pb/pb_modem_driver.h"

#include "goby/acomms/protobuf/modem_driver_status.pb.h"
#include "modemdriver_config.pb.h"

using namespace goby::common::logger;

namespace goby
{
namespace acomms
{
class ModemDriver : public goby::pb::Application
{
  public:
    ModemDriver(protobuf::ModemDriverConfig* cfg);
    ~ModemDriver();

  private:
    void loop();

    void handle_modem_data_request(protobuf::ModemTransmission* msg);
    void handle_modem_receive(const protobuf::ModemTransmission& message);

    void handle_data_response(const protobuf::ModemTransmission& message);
    void handle_initiate_transmission(const protobuf::ModemTransmission& message);

    void reset(const ModemDriverException& e);

    std::string modem_id_str() { return goby::util::as<std::string>(cfg_.driver_cfg().modem_id()); }

  private:
    protobuf::ModemDriverConfig& cfg_;

    // for PBDriver
    std::shared_ptr<goby::common::ZeroMQService> zeromq_service_;

    // for UDPDriver
    std::shared_ptr<boost::asio::io_service> asio_service_;

    std::shared_ptr<goby::acomms::ModemDriverBase> driver_;

    bool data_response_received_;
    protobuf::ModemTransmission data_response_;

    bool initiate_transmit_pending_;
    protobuf::ModemTransmission initiate_transmission_;

    bool driver_started_;

    double last_status_time_;
    goby::acomms::protobuf::ModemDriverStatus status_;
};
} // namespace acomms
} // namespace goby

int main(int argc, char* argv[])
{
    goby::acomms::protobuf::ModemDriverConfig cfg;
    goby::run<goby::acomms::ModemDriver>(argc, argv, &cfg);
}

using goby::glog;

goby::acomms::ModemDriver::ModemDriver(protobuf::ModemDriverConfig* cfg)
    : goby::pb::Application(cfg), cfg_(*cfg), data_response_received_(false),
      initiate_transmit_pending_(false), driver_started_(false), last_status_time_(0)
{
    glog.is(DEBUG1) && glog << cfg_.DebugString() << std::endl;

    switch (cfg_.driver_type())
    {
        case goby::acomms::protobuf::DRIVER_WHOI_MICROMODEM:
            driver_.reset(new goby::acomms::MMDriver);
            break;

        case goby::acomms::protobuf::DRIVER_PB_STORE_SERVER:
            zeromq_service_.reset(new goby::common::ZeroMQService);
            driver_.reset(new goby::pb::PBDriver(zeromq_service_.get()));
            break;

        case goby::acomms::protobuf::DRIVER_UDP:
            asio_service_.reset(new boost::asio::io_service);
            driver_.reset(new goby::acomms::UDPDriver(asio_service_.get()));
            break;

        case goby::acomms::protobuf::DRIVER_IRIDIUM:
            driver_.reset(new goby::acomms::IridiumDriver);

            break;

        case goby::acomms::protobuf::DRIVER_IRIDIUM_SHORE:
            driver_.reset(new goby::acomms::IridiumShoreDriver);
            break;

        default:
        case goby::acomms::protobuf::DRIVER_NONE:
            throw(goby::Exception("Invalid/unsupported driver specified"));
            break;
    }

    subscribe(&ModemDriver::handle_initiate_transmission, this,
              "Tx" + goby::util::as<std::string>(cfg_.driver_cfg().modem_id()));

    subscribe(&ModemDriver::handle_data_response, this,
              "DataResponse" + goby::util::as<std::string>(cfg_.driver_cfg().modem_id()));

    connect(&driver_->signal_receive, this, &ModemDriver::handle_modem_receive);

    connect(&driver_->signal_data_request, this, &ModemDriver::handle_modem_data_request);

    status_.set_src(cfg_.driver_cfg().modem_id());
    status_.set_status(goby::acomms::protobuf::ModemDriverStatus::NOMINAL);
}

goby::acomms::ModemDriver::~ModemDriver()
{
    if (driver_)
        driver_->shutdown();
}

void goby::acomms::ModemDriver::loop()
{
    if (driver_)
    {
        try
        {
            if (!driver_started_)
            {
                driver_->startup(cfg_.driver_cfg());
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

    double now = goby::common::goby_time<double>();
    if (last_status_time_ + cfg_.status_period_s() <= now)
    {
        status_.set_time(now);
        publish(status_, "Status" + modem_id_str());
        last_status_time_ = now;
    }

    if (initiate_transmit_pending_)
    {
        driver_->handle_initiate_transmission(initiate_transmission_);
        initiate_transmit_pending_ = false;
    }
}

void goby::acomms::ModemDriver::handle_modem_data_request(protobuf::ModemTransmission* msg)
{
    publish(*msg, "DataRequest" + modem_id_str());
    data_response_received_ = false;

    double start_time = goby::common::goby_time<double>();
    while (goby::common::goby_time<double>() < start_time + cfg_.data_request_timeout())
    {
        zeromq_service().poll(10000);
        if (data_response_received_)
        {
            *msg = data_response_;
            break;
        }
    }
    if (!data_response_received_)
        glog.is(WARN) && glog << "Timeout waiting for response to data request" << std::endl;
}

void goby::acomms::ModemDriver::handle_modem_receive(const protobuf::ModemTransmission& message)
{
    publish(message, "Rx" + modem_id_str());
}

void goby::acomms::ModemDriver::handle_data_response(const protobuf::ModemTransmission& message)
{
    data_response_received_ = true;
    data_response_ = message;
}

void goby::acomms::ModemDriver::handle_initiate_transmission(
    const protobuf::ModemTransmission& message)
{
    // wait until we enter next loop to initiate the transmission to avoid calling poll() from within poll()
    initiate_transmit_pending_ = true;
    initiate_transmission_ = message;
}

void goby::acomms::ModemDriver::reset(const ModemDriverException& e)
{
    status_.set_status(e.status());
    status_.set_n_resets(status_.n_resets() + 1);
    glog.is(WARN) && glog << "Exception: " << e.what() << std::endl;
    const int restart_sec = 15;
    glog.is(WARN) && glog << "Shutting down driver." << std::endl;
    driver_->shutdown();
    glog.is(WARN) && glog << "Attempting to restart driver in " << restart_sec << " seconds."
                          << std::endl;
    sleep(restart_sec);
    driver_started_ = false;
}
