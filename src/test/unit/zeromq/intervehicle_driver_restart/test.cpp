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

// Tests that an InterVehiclePortal with restart_on_failure set keeps running when one
// link's modem driver fails, at startup or while running, and that the failed link
// recovers without restarting the portal. The portal has two links:
//
//  - Iridium, connected over TCP to a mock modem that can stay silent or answer AT
//    commands, and can be "unplugged" (stops listening and drops the connection).
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
//     machine resets, the Iridium link reports MODEM_NOT_RESPONDING. This is not a
//     driver failure, so the driver is not restarted.
//  4. The mock modem answers again and the report returns to NOMINAL.
//  5. The mock modem is unplugged while the driver is running. The driver's next
//     write throws, and the link reports CONNECTION_TO_MODEM_FAILED rather than the
//     portal terminating. Replacement drivers then fail to connect (STARTUP_FAILED)
//     while the UDP link stays NOMINAL.
//  6. The mock modem is plugged back in. A new driver connects and the link reports
//     NOMINAL.

#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>

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

// Single-threaded TCP server: all socket handling happens on its own thread, and the
// test only flips the atomic flags.
class MockModem
{
  public:
    MockModem()
    {
        listen_on(0);
        thread_ = std::thread([this]() { run(); });
    }

    ~MockModem()
    {
        stop_ = true;
        thread_.join();
        close_fd(conn_fd_);
        close_fd(listen_fd_);
    }

    int port() const { return port_; }

    std::atomic<bool> answering{false};
    // while unplugged, connections are refused and any open one is dropped
    std::atomic<bool> unplugged{false};
    std::atomic<int> connections{0};
    // a non-zero delay holds back the +SBDIX reply, as a satellite session in progress does
    std::atomic<int> sbdix_delay_ms{0};
    std::atomic<int> sbdix_received{0};
    std::atomic<int> sbdix_answered{0};

  private:
    void listen_on(int port)
    {
        listen_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
        if (listen_fd_ < 0)
        {
            std::cerr << "FAIL: mock modem could not create a socket" << std::endl;
            exit(EXIT_FAILURE);
        }
        int one = 1;
        ::setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = htons(port);
        if (::bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 ||
            ::listen(listen_fd_, 5) != 0)
        {
            std::cerr << "FAIL: mock modem could not listen on port " << port << std::endl;
            exit(EXIT_FAILURE);
        }
        socklen_t len = sizeof(addr);
        ::getsockname(listen_fd_, reinterpret_cast<sockaddr*>(&addr), &len);
        port_ = ntohs(addr.sin_port);
    }

    static void close_fd(int& fd)
    {
        if (fd >= 0)
            ::close(fd);
        fd = -1;
    }

    void run()
    {
        std::string buffer;
        while (!stop_)
        {
            if (unplugged)
            {
                close_fd(conn_fd_);
                close_fd(listen_fd_);
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
                continue;
            }
            if (listen_fd_ < 0)
                listen_on(port_);

            pollfd fds[2] = {{listen_fd_, POLLIN, 0}, {conn_fd_, POLLIN, 0}};
            if (::poll(fds, conn_fd_ >= 0 ? 2 : 1, 50) <= 0)
                continue;

            if (fds[0].revents & POLLIN)
            {
                close_fd(conn_fd_);
                conn_fd_ = ::accept(listen_fd_, nullptr, nullptr);
                buffer.clear();
                ++connections;
            }
            else if (conn_fd_ >= 0 && fds[1].revents)
            {
                char data[256];
                auto n = ::read(conn_fd_, data, sizeof(data));
                if (n <= 0)
                {
                    close_fd(conn_fd_);
                    continue;
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
        {
            ++sbdix_received;
            std::this_thread::sleep_for(std::chrono::milliseconds(sbdix_delay_ms));
            reply = cmd + ": 0, 1, 0, 0, 0, 0\r\nOK\r\n";
            ++sbdix_answered;
        }
        else
            reply = "OK\r\n";

        if (::write(conn_fd_, reply.data(), reply.size()) < 0)
            close_fd(conn_fd_);
    }

    int listen_fd_{-1};
    int conn_fd_{-1};
    int port_{0};
    std::thread thread_;
    std::atomic<bool> stop_{false};
};

std::mutex reports_mutex;
std::map<int, ModemReport> latest_report;

ModemDriverStatus::Status latest_status(int link_modem_id)
{
    std::lock_guard<std::mutex> l(reports_mutex);
    auto it = latest_report.find(link_modem_id);
    return it == latest_report.end() ? ModemDriverStatus::NOMINAL : it->second.driver_status();
}

bool has_report(int link_modem_id)
{
    std::lock_guard<std::mutex> l(reports_mutex);
    return latest_report.count(link_modem_id);
}

template <typename Portal>
ModemDriverStatus::Status wait_for_report(Portal& portal, int link_modem_id,
                                          const std::set<ModemDriverStatus::Status>& statuses,
                                          double timeout_seconds, const std::string& step)
{
    auto end = Clock::now() + std::chrono::duration<double>(timeout_seconds);
    while (!has_report(link_modem_id) || !statuses.count(latest_status(link_modem_id)))
    {
        if (Clock::now() > end)
        {
            std::cerr << "FAIL (" << step << "): link " << link_modem_id << " still reports "
                      << ModemDriverStatus::Status_Name(latest_status(link_modem_id)) << " after "
                      << timeout_seconds << " s" << std::endl;
            exit(EXIT_FAILURE);
        }
        portal.poll(std::chrono::milliseconds(10));
    }
    auto status = latest_status(link_modem_id);
    std::cout << step << ": link " << link_modem_id << " reported "
              << ModemDriverStatus::Status_Name(status) << std::endl;
    return status;
}

template <typename Portal>
void wait_for_report(Portal& portal, int link_modem_id, ModemDriverStatus::Status status,
                     double timeout_seconds, const std::string& step)
{
    wait_for_report(portal, link_modem_id, std::set<ModemDriverStatus::Status>{status},
                    timeout_seconds, step);
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
        link.mutable_restart_on_failure()->set_interval(1);

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
    zmq_cfg.set_platform("test_driver_restart" + std::to_string(getpid()));
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
        auto portal =
            std::make_unique<goby::middleware::InterVehiclePortal<decltype(zmq)>>(zmq, portal_cfg);
        auto& intervehicle = *portal;
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
        // start_timeout (2 s) + restart_on_failure.interval (1 s) per attempt, but start_timeout
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

        modem.unplugged = true;
        // the next +CSQ (MAC slot every 5 s) is written to a closed connection
        wait_for_report(
            intervehicle, iridium_modem_id,
            {ModemDriverStatus::CONNECTION_TO_MODEM_FAILED, ModemDriverStatus::STARTUP_FAILED}, 45,
            "Step 5");
        // the replacement driver gives up on the refused TCP connection after 10 s
        wait_for_report(intervehicle, iridium_modem_id, ModemDriverStatus::STARTUP_FAILED, 45,
                        "Step 5");
        wait_for_report(intervehicle, udp_modem_id, ModemDriverStatus::NOMINAL, 5, "Step 5");
        const int connections_before_replug = modem.connections;

        modem.unplugged = false;
        // a replacement driver may be part way through a failed attempt (up to 10 s to give
        // up on the TCP connection, plus start_timeout) when the modem returns
        wait_for_report(intervehicle, iridium_modem_id, ModemDriverStatus::NOMINAL, 60, "Step 6");
        if (modem.connections <= connections_before_replug)
        {
            std::cerr << "FAIL (Step 6): NOMINAL without a new driver connection" << std::endl;
            exit(EXIT_FAILURE);
        }

        // Step 7: the portal exits during an SBD session (a mailbox check, as the slot has no
        // data). With shutdown_driver_on_exit (the default) the driver waits for the +SBDIX
        // result before the portal finishes exiting.
        const int sbdix_delay_ms = 4000;
        modem.sbdix_delay_ms = sbdix_delay_ms;
        const int sbdix_before = modem.sbdix_received;
        auto session_deadline = Clock::now() + std::chrono::seconds(30);
        while (modem.sbdix_received == sbdix_before)
        {
            if (Clock::now() > session_deadline)
            {
                std::cerr << "FAIL (Step 7): no SBD session started" << std::endl;
                exit(EXIT_FAILURE);
            }
            intervehicle.poll(std::chrono::milliseconds(10));
        }
        const auto exit_start = Clock::now();
        portal.reset();
        const double exit_seconds =
            std::chrono::duration<double>(Clock::now() - exit_start).count();
        std::cout << "Step 7: portal exited " << exit_seconds << " s after +SBDIX was sent"
                  << std::endl;
        if (modem.sbdix_answered != modem.sbdix_received ||
            exit_seconds < 0.5 * sbdix_delay_ms / 1000.0)
        {
            std::cerr << "FAIL (Step 7): portal exited before the SBD session finished"
                      << std::endl;
            exit(EXIT_FAILURE);
        }
    }

    router_context.shutdown();
    manager_context.shutdown();
    router_thread.join();
    manager_thread.join();

    std::cout << "all tests passed" << std::endl;
    return 0;
}
