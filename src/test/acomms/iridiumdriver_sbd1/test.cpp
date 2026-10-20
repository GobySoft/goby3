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

// Tests the SBD session handling of the IridiumDriver against a scripted mock
// Iridium 9603 (a TCP server speaking the AT command set). Covers the Iridium SBD
// best practices that the driver must follow:
//
//  - +CSQ is checked immediately before every +SBDIX / +SBDIXA, and no session is
//    started (and no data is pulled from the queue) when the signal is too weak.
//  - After a failed session no further session is started until the retry back-off
//    has elapsed; the back-off doubles on consecutive failures and resets on success.
//  - +SBDLOE is queried at startup and after MO status 38 (traffic management), and
//    no session (including a ring alert response) is started until the reported
//    traffic management period has ended.
//  - Acks for received MT messages go through the same +CSQ check, and a weak signal
//    is re-checked after a delay rather than polled continuously.
//  - When a session reports further MT messages queued at the gateway, they are
//    retrieved straight away with mailbox checks.
//  - Sessions started by the MAC are delayed by a random amount (sbd_session_jitter).
//
// The back-off values are shortened from the defaults so that the test runs quickly.

#include <atomic>
#include <cassert>
#include <chrono>
#include <deque>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/write.hpp>

#include "goby/acomms/modemdriver/iridium_driver.h"
#include "goby/acomms/modemdriver/iridium_driver_common.h"
#include "goby/acomms/modemdriver/iridium_sbd_packet.h"
#include "goby/util/debug_logger.h"

using goby::glog;
using namespace goby::util::logger;
using Clock = std::chrono::steady_clock;

class MockISU
{
  public:
    MockISU() : acceptor_(io_, boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), 0))
    {
        thread_ = std::thread([this]() { run(); });
    }

    // the reader thread exits once the driver closes the connection after stop_after_disconnect(),
    // so the socket is never closed from this thread while it is being read
    ~MockISU() { thread_.join(); }

    // otherwise the mock accepts a new connection (driver restart) after a disconnect
    void stop_after_disconnect() { stop_ = true; }

    int port() const { return acceptor_.local_endpoint().port(); }

    void send_unsolicited(const std::string& line) { write(line + "\r\n"); }

    // AT commands received (without "AT" prefix or "\r")
    std::vector<std::string> commands()
    {
        std::lock_guard<std::mutex> l(log_mutex_);
        return commands_;
    }

    std::atomic<int> csq{3};
    std::atomic<int> sbdix_mo_status{0};
    std::atomic<int> sbdloe_time{0};
    // if >= 0, csq is set to this after the MT message is read with +SBDRB
    std::atomic<int> csq_after_sbdrb{-1};

    // delivered (MT status 1), one per successful +SBDIX, in order
    void add_mt_message(const std::string& sbd_packet)
    {
        std::lock_guard<std::mutex> l(mt_mutex_);
        mt_messages_.push_back(sbd_packet);
    }

  private:
    void run()
    {
        do {
            boost::asio::ip::tcp::socket socket(io_);
            boost::system::error_code ec;
            acceptor_.accept(socket, ec);
            if (ec)
                return;
            {
                std::lock_guard<std::mutex> l(write_mutex_);
                socket_ = std::move(socket);
            }
            serve();
        } while (!stop_);
    }

    void serve()
    {
        boost::system::error_code ec;
        std::string buffer;
        std::size_t binary_remaining = 0;
        char data[256];
        for (;;)
        {
            std::size_t n = socket_.read_some(boost::asio::buffer(data), ec);
            if (ec)
            {
                std::lock_guard<std::mutex> l(write_mutex_);
                socket_.close(ec);
                return;
            }
            buffer.append(data, n);

            for (;;)
            {
                if (binary_remaining > 0)
                {
                    if (buffer.size() < binary_remaining)
                        break;
                    buffer.erase(0, binary_remaining);
                    binary_remaining = 0;
                    write("0\r\nOK\r\n");
                    continue;
                }

                auto cr = buffer.find('\r');
                if (cr == std::string::npos)
                    break;
                std::string line = buffer.substr(0, cr);
                buffer.erase(0, cr + 1);
                if (line.compare(0, 2, "AT") != 0)
                    continue;
                std::string cmd = line.substr(2);
                {
                    std::lock_guard<std::mutex> l(log_mutex_);
                    commands_.push_back(cmd);
                }
                binary_remaining = respond(cmd);
            }
        }
    }

    // returns number of binary bytes to expect next
    std::size_t respond(const std::string& cmd)
    {
        const std::string sbdwb = "+SBDWB=";
        if (cmd == "+CSQ")
        {
            write("+CSQ:" + std::to_string(csq) + "\r\nOK\r\n");
        }
        else if (cmd == "+SBDLOE")
        {
            write("+SBDLOE:0," + std::to_string(sbdloe_time) + "\r\nOK\r\n");
        }
        else if (cmd == "+SBDD2")
        {
            write("0\r\nOK\r\n");
        }
        else if (cmd.compare(0, sbdwb.size(), sbdwb) == 0)
        {
            write("READY\r\n");
            const int checksum_bytes = 2;
            return std::stoi(cmd.substr(sbdwb.size())) + checksum_bytes;
        }
        else if (cmd == "+SBDIX" || cmd == "+SBDIXA")
        {
            std::lock_guard<std::mutex> l(mt_mutex_);
            const bool mt = !mt_messages_.empty() && sbdix_mo_status <= 4;
            const auto mt_length = mt ? mt_messages_.front().size() : 0;
            const auto mt_queued = mt ? mt_messages_.size() - 1 : 0;
            write(cmd + ": " + std::to_string(sbdix_mo_status) + ", 1, " + (mt ? "1" : "0") +
                  ", 0, " + std::to_string(mt_length) + ", " + std::to_string(mt_queued) +
                  "\r\nOK\r\n");
            mt_pending_read_ = mt;
        }
        else if (cmd == "+SBDRB")
        {
            std::lock_guard<std::mutex> l(mt_mutex_);
            if (mt_pending_read_)
            {
                const std::string& mt_message = mt_messages_.front();
                unsigned csum = 0;
                for (char c : mt_message) csum += c & 0xFF;
                std::string out;
                out += static_cast<char>((mt_message.size() >> 8) & 0xFF);
                out += static_cast<char>(mt_message.size() & 0xFF);
                out += mt_message;
                out += static_cast<char>((csum >> 8) & 0xFF);
                out += static_cast<char>(csum & 0xFF);
                write(out + "\r\nOK\r\n");
                mt_messages_.pop_front();
                mt_pending_read_ = false;
                if (csq_after_sbdrb >= 0)
                    csq = csq_after_sbdrb.exchange(-1);
            }
        }
        else
        {
            write("OK\r\n");
        }
        return 0;
    }

    void write(const std::string& s)
    {
        std::lock_guard<std::mutex> l(write_mutex_);
        boost::system::error_code ec;
        boost::asio::write(socket_, boost::asio::buffer(s), ec);
    }

    boost::asio::io_context io_;
    boost::asio::ip::tcp::acceptor acceptor_;
    boost::asio::ip::tcp::socket socket_{io_};
    std::thread thread_;
    std::mutex write_mutex_;
    std::mutex log_mutex_;
    std::vector<std::string> commands_;
    std::mutex mt_mutex_;
    std::deque<std::string> mt_messages_;
    bool mt_pending_read_{false};
    std::atomic<bool> stop_{false};
};

std::unique_ptr<MockISU> isu;
std::unique_ptr<goby::acomms::IridiumDriver> driver;
int data_requests = 0;
int receives = 0;

void run_for(double seconds)
{
    auto end = Clock::now() + std::chrono::duration<double>(seconds);
    while (Clock::now() < end)
    {
        driver->do_work();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

void initiate()
{
    goby::acomms::protobuf::ModemTransmission msg;
    msg.set_src(1);
    msg.set_dest(2);
    msg.set_rate(goby::acomms::RATE_SBD);
    msg.set_type(goby::acomms::protobuf::ModemTransmission::DATA);
    driver->handle_initiate_transmission(msg);
}

// number of commands received since index 'since' that equal cmd (or start with cmd, if it ends in '=')
int count(const std::string& cmd, std::size_t since)
{
    auto cmds = isu->commands();
    const bool prefix = !cmd.empty() && cmd.back() == '=';
    int n = 0;
    for (auto i = since; i < cmds.size(); ++i)
        if (prefix ? cmds[i].compare(0, cmd.size(), cmd) == 0 : cmds[i] == cmd)
            ++n;
    return n;
}

std::size_t mark() { return isu->commands().size(); }

// SBD packet for a message from the shore (modem 2) to the vehicle (modem 1)
std::string mt_packet(bool ack_requested)
{
    goby::acomms::protobuf::ModemTransmission mt;
    mt.set_src(2);
    mt.set_dest(1);
    mt.set_rate(goby::acomms::RATE_SBD);
    mt.set_type(goby::acomms::protobuf::ModemTransmission::DATA);
    mt.set_ack_requested(ack_requested);
    mt.set_frame_start(0);
    mt.add_frame("hi");
    std::string iridium_packet, sbd_packet;
    goby::acomms::serialize_iridium_modem_message(&iridium_packet, mt);
    goby::acomms::serialize_sbd_packet(iridium_packet, &sbd_packet);
    return sbd_packet;
}

int main(int argc, char* argv[])
{
    goby::glog.add_stream(goby::util::logger::DEBUG1, &std::clog);
    goby::glog.set_name(argv[0]);

    isu.reset(new MockISU);
    driver.reset(new goby::acomms::IridiumDriver);

    driver->signal_data_request.connect(
        [](goby::acomms::protobuf::ModemTransmission* msg)
        {
            ++data_requests;
            msg->add_frame("hello");
        });
    driver->signal_receive.connect([](const goby::acomms::protobuf::ModemTransmission& msg)
                                   { ++receives; });

    goby::acomms::protobuf::DriverConfig cfg;
    cfg.set_modem_id(1);
    cfg.set_connection_type(goby::acomms::protobuf::DriverConfig::CONNECTION_TCP_AS_CLIENT);
    cfg.set_tcp_server("127.0.0.1");
    cfg.set_tcp_port(isu->port());
    cfg.set_reconnect_interval(1);
    auto* iridium_cfg = cfg.MutableExtension(goby::acomms::iridium::protobuf::config);
    iridium_cfg->set_device(goby::acomms::iridium::protobuf::DEVICE_IRIDIUM_9602_9603);
    iridium_cfg->set_sbd_retry_backoff_initial(2);
    iridium_cfg->set_sbd_retry_backoff_max(4);
    iridium_cfg->set_sbd_retry_jitter_fraction(0);
    iridium_cfg->set_sbd_session_jitter(0);

    // 1. Traffic management reported at startup blocks the first session
    {
        isu->sbdloe_time = 3;
        driver->startup(cfg);
        auto startup_done = Clock::now();
        assert(count("+SBDLOE", 0) == 1);
        isu->sbdloe_time = 0;

        auto m = mark();
        initiate();
        run_for(1);
        assert(count("+CSQ", m) == 0);
        assert(data_requests == 0);

        run_for(3.5 - std::chrono::duration<double>(Clock::now() - startup_done).count());
        m = mark();
        initiate();
        run_for(1);
        assert(count("+CSQ", m) == 1);
        assert(count("+SBDWB=", m) == 1);
        assert(count("+SBDIX", m) == 1);
        assert(data_requests == 1);
        std::cout << "1. startup traffic management: pass" << std::endl;
    }

    // 2. Insufficient signal: no session, no data request
    {
        isu->csq = 1;
        auto m = mark();
        initiate();
        run_for(1);
        assert(count("+CSQ", m) == 1);
        assert(count("+SBDWB=", m) == 0);
        assert(count("+SBDIX", m) == 0);
        assert(data_requests == 1);
        isu->csq = 3;
        std::cout << "2. insufficient signal: pass" << std::endl;
    }

    // 3. Retry back-off after failed sessions (2 s, then 4 s, reset on success)
    {
        isu->sbdix_mo_status = 32;
        auto m = mark();
        initiate();
        run_for(1);
        assert(count("+SBDIX", m) == 1);

        m = mark();
        initiate();
        run_for(0.5);
        assert(count("+CSQ", m) == 0);

        run_for(1);
        m = mark();
        initiate();
        run_for(1);
        assert(count("+SBDIX", m) == 1); // second failure: 4 s back-off

        m = mark();
        run_for(2);
        initiate();
        run_for(0.5);
        assert(count("+CSQ", m) == 0);

        isu->sbdix_mo_status = 0;
        run_for(2);
        m = mark();
        initiate();
        run_for(1);
        assert(count("+SBDIX", m) == 1);

        m = mark();
        initiate();
        run_for(1);
        assert(count("+SBDIX", m) == 1); // success: no back-off
        std::cout << "3. retry back-off: pass" << std::endl;
    }

    // 4. MO status 38: +SBDLOE is queried and the traffic management period is respected,
    // including for ring alerts
    {
        isu->sbdix_mo_status = 38;
        isu->sbdloe_time = 5;
        auto m = mark();
        initiate();
        run_for(1);
        assert(count("+SBDIX", m) == 1);
        assert(count("+SBDLOE", m) == 1);
        auto cmds = isu->commands();
        assert(cmds.back() == "+SBDLOE");
        isu->sbdix_mo_status = 0;
        isu->sbdloe_time = 0;

        run_for(2); // past the 2 s retry back-off, but inside the 5 s traffic management
        m = mark();
        isu->send_unsolicited("SBDRING");
        run_for(0.5);
        initiate();
        run_for(0.5);
        assert(count("+CSQ", m) == 0);

        run_for(2);
        m = mark();
        isu->send_unsolicited("SBDRING");
        run_for(1);
        assert(count("+CSQ", m) == 1);
        assert(count("+SBDIXA", m) == 1);
        assert(count("+SBDWB=", m) == 0); // mailbox check
        std::cout << "4. traffic management: pass" << std::endl;
    }

    // 5. Every +SBDIX[A] was immediately preceded by a +CSQ in the same session
    {
        auto cmds = isu->commands();
        bool csq_since_last_session = false;
        int sessions = 0;
        for (const auto& c : cmds)
        {
            if (c == "+CSQ")
                csq_since_last_session = true;
            else if (c == "+SBDIX" || c == "+SBDIXA")
            {
                assert(csq_since_last_session);
                csq_since_last_session = false;
                ++sessions;
            }
        }
        assert(sessions == 7);
        std::cout << "5. +CSQ before every session: pass" << std::endl;
    }

    // 6. An ack for a received MT message waits for sufficient signal, re-checking
    // +CSQ after a delay rather than continuously
    {
        isu->add_mt_message(mt_packet(true));
        isu->csq_after_sbdrb = 1;

        auto m = mark();
        initiate();
        run_for(5);
        assert(count("+SBDRB", m) == 1);
        assert(receives == 1);
        // one session for the data, then a single low-signal check for the ack
        assert(count("+CSQ", m) == 2);
        assert(count("+SBDIX", m) == 1);

        isu->csq = 3;
        run_for(7); // 10 s re-check delay
        assert(count("+CSQ", m) == 3);
        assert(count("+SBDWB=", m) == 2);
        assert(count("+SBDIX", m) == 2);
        assert(data_requests == 7); // the ack is not pulled from the queue
        std::cout << "6. ack waits for signal: pass" << std::endl;
    }

    // 7. MT messages still queued at the gateway are retrieved straight away
    {
        isu->add_mt_message(mt_packet(false));
        isu->add_mt_message(mt_packet(false));

        auto m = mark();
        initiate();
        run_for(3);
        assert(count("+SBDIX", m) == 2);
        assert(count("+SBDRB", m) == 2);
        assert(count("+SBDWB=", m) == 1); // the follow-up is a mailbox check
        assert(receives == 3);
        assert(data_requests == 8);

        run_for(2);
        assert(count("+SBDIX", m) == 2); // nothing left queued
        std::cout << "7. queued MT retrieved: pass" << std::endl;
    }

    // 8. MAC-initiated sessions are delayed by up to sbd_session_jitter
    {
        const double jitter = 2;
        iridium_cfg->set_sbd_session_jitter(jitter);
        driver->startup(cfg);

        double max_delay = 0;
        for (int i = 0; i < 5; ++i)
        {
            auto m = mark();
            auto start = Clock::now();
            initiate();
            while (count("+CSQ", m) == 0 && Clock::now() < start + std::chrono::seconds(5))
            {
                driver->do_work();
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            double delay = std::chrono::duration<double>(Clock::now() - start).count();
            std::cout << "session start delay: " << delay << " s" << std::endl;
            assert(count("+CSQ", m) == 1);
            assert(delay < jitter + 0.5);
            max_delay = std::max(max_delay, delay);
            run_for(1);
            assert(count("+SBDIX", m) == 1);
        }
        // all five below 0.1 s has probability (0.1/2)^5, about 3e-7
        assert(max_delay > 0.1);
        std::cout << "8. session jitter: pass" << std::endl;
    }

    isu->stop_after_disconnect();
    driver->shutdown();
    driver.reset();
    isu.reset();

    std::cout << "all tests passed" << std::endl;
    return 0;
}
