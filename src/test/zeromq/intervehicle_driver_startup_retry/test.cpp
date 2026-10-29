// Copyright 2026:
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

// Tests that an InterVehiclePortal with startup_retry_interval set keeps running when
// one link's modem driver fails to start, and that the failed link recovers without
// restarting the portal. The portal has two links:
//
//  - Iridium, connected over TCP to a mock modem that can be switched between
//    answering AT commands and staying silent.
//  - UDP multicast, which always starts.
//
// Steps:
//  1. The mock modem is silent. The portal constructor must return. The UDP link
//     reports NOMINAL, and the Iridium link reports STARTUP_FAILED and
//     LINK_NOT_AVAILABLE while it keeps retrying (a new TCP connection per attempt).
//  2. The mock modem starts answering. The next attempt succeeds and the Iridium link
//     reports NOMINAL.
//  3. The mock modem goes silent again after a successful start. Once the driver's
//     AT commands (here the +CSQ of a mailbox check) go unanswered until its state
//     machine resets, the Iridium link reports MODEM_NOT_RESPONDING.
//  4. The mock modem answers again and the report returns to NOMINAL.

#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <iostream>
#include <map>
#include <mutex>
#include <string>
#include <thread>

#include <boost/asio/connect.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/write.hpp>

#include "goby/acomms/modemdriver/iridium_driver_common.h"
#include "goby/acomms/protobuf/iridium_driver.pb.h"
#include "goby/acomms/protobuf/udp_multicast_driver.pb.h"
#include "goby/middleware/marshalling/protobuf.h"
#include "goby/middleware/transport/intervehicle.h"
#include "goby/util/debug_logger.h"
#include "goby/zeromq/transport/interprocess.h"

using goby::glog;
using namespace goby::util::logger;
using Clock = std::chrono::steady_clock;
using goby::acomms::protobuf::ModemDriverStatus;
using goby::acomms::protobuf::ModemReport;

const int iridium_modem_id = 1;
const int udp_modem_id = 17;

class MockModem
{
  public:
    MockModem() : acceptor_(io_, boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), 0))
    {
        thread_ = std::thread([this]() { run(); });
    }

    ~MockModem()
    {
        stop_ = true;
        {
            std::lock_guard<std::mutex> l(socket_mutex_);
            if (socket_.is_open())
                ::shutdown(socket_.native_handle(), SHUT_RDWR);
        }
        // unblock accept()
        boost::asio::ip::tcp::socket wake(io_);
        boost::system::error_code ec;
        wake.connect(
            boost::asio::ip::tcp::endpoint(boost::asio::ip::address_v4::loopback(), port()), ec);
        thread_.join();
    }

    int port() const { return acceptor_.local_endpoint().port(); }

    std::atomic<bool> answering{false};
    std::atomic<int> connections{0};

  private:
    void run()
    {
        while (!stop_)
        {
            boost::asio::ip::tcp::socket socket(io_);
            boost::system::error_code ec;
            acceptor_.accept(socket, ec);
            if (ec || stop_)
                return;
            ++connections;
            {
                std::lock_guard<std::mutex> l(socket_mutex_);
                socket_ = std::move(socket);
            }
            serve();
        }
    }

    void serve()
    {
        boost::system::error_code ec;
        std::string buffer;
        char data[256];
        for (;;)
        {
            std::size_t n = socket_.read_some(boost::asio::buffer(data), ec);
            if (ec)
            {
                std::lock_guard<std::mutex> l(socket_mutex_);
                socket_.close(ec);
                return;
            }
            buffer.append(data, n);

            std::string::size_type cr;
            while ((cr = buffer.find('\r')) != std::string::npos)
            {
                std::string line = buffer.substr(0, cr);
                buffer.erase(0, cr + 1);
                if (answering && line.compare(0, 2, "AT") == 0)
                    respond(line.substr(2));
            }
        }
    }

    void respond(const std::string& cmd)
    {
        std::string reply;
        if (cmd == "+CSQ")
            reply = "+CSQ:5\r\nOK\r\n";
        else if (cmd == "+SBDLOE")
            reply = "+SBDLOE:0,0\r\nOK\r\n";
        else if (cmd == "+SBDD2")
            reply = "0\r\nOK\r\n";
        else if (cmd == "+SBDIX" || cmd == "+SBDIXA")
            reply = cmd + ": 0, 1, 0, 0, 0, 0\r\nOK\r\n";
        else
            reply = "OK\r\n";

        boost::system::error_code ec;
        boost::asio::write(socket_, boost::asio::buffer(reply), ec);
    }

    boost::asio::io_context io_;
    boost::asio::ip::tcp::acceptor acceptor_;
    boost::asio::ip::tcp::socket socket_{io_};
    std::mutex socket_mutex_;
    std::thread thread_;
    std::atomic<bool> stop_{false};
};

std::mutex reports_mutex;
std::map<int, ModemReport> latest_report;

bool has_report(int link_modem_id, ModemDriverStatus::Status status)
{
    std::lock_guard<std::mutex> l(reports_mutex);
    auto it = latest_report.find(link_modem_id);
    return it != latest_report.end() && it->second.driver_status() == status;
}

template <typename Portal>
void wait_for_report(Portal& portal, int link_modem_id, ModemDriverStatus::Status status,
                     double timeout_seconds, const std::string& step)
{
    auto end = Clock::now() + std::chrono::duration<double>(timeout_seconds);
    while (!has_report(link_modem_id, status))
    {
        if (Clock::now() > end)
        {
            std::cerr << "FAIL (" << step << "): no " << ModemDriverStatus::Status_Name(status)
                      << " report for link " << link_modem_id << " within " << timeout_seconds
                      << " s" << std::endl;
            exit(EXIT_FAILURE);
        }
        portal.poll(std::chrono::milliseconds(10));
    }
    std::cout << step << ": link " << link_modem_id << " reported "
              << ModemDriverStatus::Status_Name(status) << std::endl;
}

int main(int argc, char* argv[])
{
    goby::glog.add_stream(goby::util::logger::WARN, &std::cerr);
    goby::glog.set_name(argv[0]);
    goby::glog.set_lock_action(goby::util::logger_lock::lock);

    MockModem modem;

    goby::middleware::intervehicle::protobuf::PortalConfig portal_cfg;
    {
        auto& link = *portal_cfg.add_link();
        link.set_modem_id(iridium_modem_id);
        link.set_modem_report_interval(0.5);
        link.set_startup_retry_interval(1);

        auto& driver = *link.mutable_driver();
        driver.set_driver_type(goby::acomms::protobuf::DRIVER_IRIDIUM);
        driver.set_connection_type(goby::acomms::protobuf::DriverConfig::CONNECTION_TCP_AS_CLIENT);
        driver.set_tcp_server("127.0.0.1");
        driver.set_tcp_port(modem.port());
        auto& iridium = *driver.MutableExtension(goby::acomms::iridium::protobuf::config);
        iridium.set_start_timeout(2);
        iridium.set_sbd_session_jitter(0);

        auto& mac = *link.mutable_mac();
        mac.set_type(goby::acomms::protobuf::MAC_FIXED_DECENTRALIZED);
        auto& slot = *mac.add_slot();
        slot.set_src(iridium_modem_id);
        slot.set_rate(goby::acomms::RATE_SBD);
        slot.set_slot_seconds(5);
    }
    {
        auto& link = *portal_cfg.add_link();
        link.set_modem_id(udp_modem_id);
        link.set_modem_report_interval(0.5);

        auto& driver = *link.mutable_driver();
        driver.set_driver_type(goby::acomms::protobuf::DRIVER_UDP_MULTICAST);
        driver.MutableExtension(goby::acomms::udp_multicast::protobuf::config)
            ->set_multicast_port(61000 + getpid() % 1000);

        auto& mac = *link.mutable_mac();
        mac.set_type(goby::acomms::protobuf::MAC_FIXED_DECENTRALIZED);
        auto& slot = *mac.add_slot();
        slot.set_src(1);
        slot.set_slot_seconds(1);
    }

    goby::zeromq::protobuf::InterProcessPortalConfig zmq_cfg;
    zmq_cfg.set_platform("test_startup_retry" + std::to_string(getpid()));
    zmq_cfg.set_client_name("portal");

    zmq::context_t router_context(1), manager_context(1);
    goby::zeromq::Router router(router_context, zmq_cfg);
    std::thread router_thread([&] { router.run(); });
    goby::zeromq::Manager manager(manager_context, zmq_cfg, router);
    std::thread manager_thread([&] { manager.run(); });

    {
        goby::zeromq::InterProcessPortal<goby::middleware::InterThreadTransporter> zmq(zmq_cfg);
        zmq.subscribe<goby::middleware::intervehicle::groups::modem_report>(
            [](const goby::middleware::intervehicle::protobuf::ModemReportWithLinkID& r)
            {
                std::lock_guard<std::mutex> l(reports_mutex);
                latest_report[r.link_modem_id()] = r.data();
            });

        // before the fix, this either aborted the process or never returned
        goby::middleware::InterVehiclePortal<decltype(zmq)> intervehicle(zmq, portal_cfg);
        std::cout << "Portal constructed with the Iridium modem silent" << std::endl;

        wait_for_report(intervehicle, udp_modem_id, ModemDriverStatus::NOMINAL, 10, "Step 1");
        wait_for_report(intervehicle, iridium_modem_id, ModemDriverStatus::STARTUP_FAILED, 10,
                        "Step 1");
        {
            std::lock_guard<std::mutex> l(reports_mutex);
            if (latest_report[iridium_modem_id].link_state() != ModemReport::LINK_NOT_AVAILABLE)
            {
                std::cerr << "FAIL (Step 1): Iridium link not LINK_NOT_AVAILABLE" << std::endl;
                exit(EXIT_FAILURE);
            }
        }
        // start_timeout (2 s) + startup_retry_interval (1 s) per attempt, but start_timeout
        // counts do_work() iterations so it stretches when the machine is loaded
        auto retry_deadline = Clock::now() + std::chrono::seconds(45);
        while (modem.connections < 3)
        {
            if (Clock::now() > retry_deadline)
            {
                std::cerr << "FAIL (Step 1): startup not retried (connections: "
                          << modem.connections << ")" << std::endl;
                exit(EXIT_FAILURE);
            }
            intervehicle.poll(std::chrono::milliseconds(10));
        }
        std::cout << "Step 1: " << modem.connections << " startup attempts so far" << std::endl;

        modem.answering = true;
        wait_for_report(intervehicle, iridium_modem_id, ModemDriverStatus::NOMINAL, 15, "Step 2");
        const int connections_after_start = modem.connections;

        modem.answering = false;
        // +CSQ: 15 s timeout x (1 + 3 retries) before the state machine resets
        wait_for_report(intervehicle, iridium_modem_id, ModemDriverStatus::MODEM_NOT_RESPONDING, 90,
                        "Step 3");
        if (modem.connections != connections_after_start)
        {
            std::cerr << "FAIL (Step 3): driver was restarted rather than reporting" << std::endl;
            exit(EXIT_FAILURE);
        }

        modem.answering = true;
        wait_for_report(intervehicle, iridium_modem_id, ModemDriverStatus::NOMINAL, 30, "Step 4");
    }

    router_context.shutdown();
    manager_context.shutdown();
    router_thread.join();
    manager_thread.join();

    std::cout << "all tests passed" << std::endl;
    return 0;
}
