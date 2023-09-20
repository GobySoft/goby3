// Copyright 2023:
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

#include <boost/asio/read.hpp>
#include <boost/bimap.hpp>

#include "goby/middleware/marshalling/protobuf.h"
// this space intentionally left blank
#include "goby/middleware/io/line_based/pty.h"
#include "goby/zeromq/application/multi_thread.h"

#include "goby/acomms/modemdriver/iridium_driver_fsm.h"
#include "goby/acomms/protobuf/iridium_driver.pb.h"
#include "goby/acomms/protobuf/rockblock_simulator_config.pb.h"

#include "goby/util/thirdparty/cpp-httplib/httplib.h"
#include "goby/util/thirdparty/nlohmann/json.hpp"

using goby::glog;

constexpr goby::middleware::Group pty_in{"pty_in"};
constexpr goby::middleware::Group pty_out{"pty_out"};
constexpr goby::middleware::Group mtdata{"mtdata"};

namespace goby
{
namespace apps
{
namespace acomms
{
constexpr int SBD_CHECKSUM_BYTES{2};
class RockBLOCKSimulator
    : public middleware::MultiThreadStandaloneApplication<protobuf::RockBLOCKSimulatorConfig>
{
  public:
    RockBLOCKSimulator();
    ~RockBLOCKSimulator();

  private:
    void loop() override;
    void process_command_data(const goby::middleware::protobuf::IOData& io);
    void send_response(int index, const std::string& command, const std::string& response);
    void write_mo_message(int index);

  private:
    struct ModemData
    {
        enum class State
        {
            ONLINE, // not implemented for RockBLOCK sim
            COMMAND
        };
        State state{State::COMMAND};

        std::shared_ptr<std::string> mt_message_pending;
        std::shared_ptr<std::string> mt_message;

        int mo_pending_write_size{0};
        std::unique_ptr<std::string> mo_message;
        int mtmsn{0};
        int momsn{0};
    };

    // pty index to ModemData
    std::map<int, ModemData> modem_data_;

    // maps IMEI to Goby Modem ID
    boost::bimap<std::string, int> imei_to_id_;

    // maps Goby Modem ID to pty index
    boost::bimap<int, int> id_to_pty_index_;

    struct CIEVState
    {
        bool rssi{false};
        bool svcind{false};
    };
    CIEVState ciev_state_;
};

class RockBLOCKMTHTTPEndpointThread
    : public goby::middleware::SimpleThread<protobuf::RockBLOCKSimulatorConfig>
{
  public:
    RockBLOCKMTHTTPEndpointThread(const protobuf::RockBLOCKSimulatorConfig cfg);
    ~RockBLOCKMTHTTPEndpointThread() {}

  private:
    int mtmsn_{0};
    std::set<std::string> imei_in_use_;
};

class RockBLOCKPTYThread
    : public goby::middleware::io::detail::PTYThread<
          pty_in, pty_out, goby::middleware::io::PubSubLayer::INTERTHREAD,
          goby::middleware::io::PubSubLayer::INTERTHREAD, goby::middleware::SimpleThread>
{
    using Base = goby::middleware::io::detail::PTYThread<
        pty_in, pty_out, goby::middleware::io::PubSubLayer::INTERTHREAD,
        goby::middleware::io::PubSubLayer::INTERTHREAD, goby::middleware::SimpleThread>;

  public:
    RockBLOCKPTYThread(const goby::middleware::protobuf::PTYConfig& config, int index)
        : Base(config, index)
    {
    }

    ~RockBLOCKPTYThread() {}

  private:
    void async_read() override;

  private:
    boost::asio::streambuf buffer_;
    int mo_pending_write_size_{0};
};

} // namespace acomms
} // namespace apps
} // namespace goby

int main(int argc, char* argv[])
{
    return goby::run<goby::apps::acomms::RockBLOCKSimulator>(argc, argv);
}

//
// RockBLOCKSimulator
//

goby::apps::acomms::RockBLOCKSimulator::RockBLOCKSimulator()
    : middleware::MultiThreadStandaloneApplication<protobuf::RockBLOCKSimulatorConfig>(
          0.2 * boost::units::si::hertz)
{
    glog.add_group("http", goby::util::Colors::yellow);

    auto pty_index = 0;

    interthread().subscribe<pty_in>(
        [this](const goby::middleware::protobuf::IOData& io)
        {
            switch (modem_data_[io.index()].state)
            {
                case ModemData::State::COMMAND: process_command_data(io); break;
                case ModemData::State::ONLINE:
                    glog.is_die() && glog << "Online mode not implemented" << std::endl;
                    break;
            }
        });

    interthread().subscribe<mtdata>(
        [this](const nlohmann::json& jdata)
        {
            auto imei = jdata["imei"].get<std::string>();
            auto modem_id = imei_to_id_.left.at(imei);
            auto index = id_to_pty_index_.left.at(modem_id);
            auto& modem_data = modem_data_[index];
            modem_data.mtmsn = jdata["mtmsn"].get<int>();
            modem_data.mt_message_pending.reset(
                new std::string(goby::util::hex_decode(jdata["data"].get<std::string>())));

            auto io_response = std::make_shared<goby::middleware::protobuf::IOData>();
            io_response->set_index(index);
            io_response->set_data("SBDRING\r\n");
            interthread().publish<pty_out>(io_response);
        });

    imei_to_id_.clear();
    for (const auto& imei_to_id : cfg().imei_to_id())
    {
        imei_to_id_.left.insert(std::make_pair(imei_to_id.imei(), imei_to_id.modem_id()));
        id_to_pty_index_.left.insert(std::make_pair(imei_to_id.modem_id(), pty_index));
        launch_thread<RockBLOCKPTYThread>(pty_index++, imei_to_id.pty());
    }

    launch_thread<RockBLOCKMTHTTPEndpointThread>(cfg());
}

goby::apps::acomms::RockBLOCKSimulator::~RockBLOCKSimulator() {}

void goby::apps::acomms::RockBLOCKSimulator::process_command_data(
    const goby::middleware::protobuf::IOData& io)
{
    auto& modem_data = modem_data_[io.index()];
    //    auto modem_id = id_to_pty_index_.right.find(io.index())->second;

    std::string command = io.data();

    // default response
    std::string response = "OK";

    if (modem_data.mo_pending_write_size > 0)
    {
        std::string data = (command.size() > modem_data.mo_pending_write_size)
                               ? command.substr(0, modem_data.mo_pending_write_size)
                               : command;

        *modem_data.mo_message += data;
        modem_data.mo_pending_write_size -= data.size();

        //Note: AT+SBDWB returns one of the 4 responses above (0, 1, 2, 3) with 0 indicating success. In
        //all cases except response 1, the response is followed by result code ‘OK’. This could be considered
        //a bug, but has been present since the very first SBD release so has not been fixed for fear of
        //breaking existing applications
        if (modem_data.mo_pending_write_size == 0)
        {
            unsigned provided_csum = 0;
            const int bits_in_byte = 8;
            provided_csum |= (((*modem_data.mo_message)[modem_data.mo_message->size() - 2] & 0xFF)
                              << bits_in_byte);
            provided_csum |= ((*modem_data.mo_message)[modem_data.mo_message->size() - 1] & 0xFF);

            if (goby::acomms::iridium::sbd_csum(modem_data.mo_message->substr(
                    0, modem_data.mo_message->size() - SBD_CHECKSUM_BYTES)) == provided_csum)
            {
                modem_data.mo_message->erase(modem_data.mo_message->size() - SBD_CHECKSUM_BYTES);
                send_response(io.index(), command,
                              "0\r\nOK"); //SBD message successfully written to the ISU.
            }
            else
            {
                modem_data.mo_message.reset();
                send_response(
                    io.index(), command,
                    "2\r\nOK"); //SBD message checksum sent from DTE does not match the checksum calculated at the ISU.
            }
        }

        return;
    }

    boost::trim(command);

    if (command.substr(0, 2) != "AT")
    {
        response = "ERROR";
        send_response(io.index(), command, response);
        return;
    }

    const std::string sbdi = "AT+SBDI";
    const std::string clear_buffer = "AT+SBDD2";
    const std::string write_buffer = "AT+SBDWB";
    const std::string read_buffer = "AT+SBDRB";
    const std::string cier = "AT+CIER";
    if (command.substr(0, sbdi.size()) == sbdi) // mailbox check / send message
    {
        modem_data.mt_message = modem_data.mt_message_pending;
        modem_data.mt_message_pending.reset();

        int mo_status = 0;                           // MO message, if any, transferred successfully
        int mt_status = (modem_data.mt_message) ? 1  // received message
                                                : 0; // no message
        int mt_length = (modem_data.mt_message) ? modem_data.mt_message->size() : 0;
        int mt_queued = 0;

        response = std::string("+SBDI") + ((command.size() > sbdi.size()) ? "X" : "") + ": " +
                   std::to_string(mo_status) + ", " + std::to_string(modem_data.momsn) + ", " +
                   std::to_string(mt_status) + ", " + std::to_string(modem_data.mtmsn) + ", " +
                   std::to_string(mt_length) + ", " + std::to_string(mt_queued) + "\r\n\r\nOK";

        if (modem_data.mo_message)
        {
            // send message to shore
            write_mo_message(io.index());
        }
        ++modem_data.momsn;
    }
    else if (command.substr(0, clear_buffer.size()) == clear_buffer)
    {
        modem_data.mo_message.reset();
        modem_data.mt_message.reset();
        response = "0\r\n\r\nOK"; //Buffer(s) cleared successfully
    }
    else if (command.substr(0, write_buffer.size()) == write_buffer)
    {
        modem_data.mo_message.reset(new std::string());

        modem_data.mo_pending_write_size =
            goby::util::as<int>(command.substr(command.find('=') + 1)) + SBD_CHECKSUM_BYTES;
        glog.is_debug1() && glog << "Waiting for " << modem_data.mo_pending_write_size << " bytes"
                                 << std::endl;
        response = "READY";
    }
    else if (command.substr(0, read_buffer.size()) == read_buffer)
    {
        if (modem_data.mt_message)
        {
            enum
            {
                SBD_FIELD_SIZE_BYTES = 2,
                SBD_BITS_IN_BYTE = 8,
            };
            std::string rx_msg{0, SBD_FIELD_SIZE_BYTES};
            unsigned message_size = modem_data.mt_message->size();
            rx_msg[0] = (message_size >> SBD_BITS_IN_BYTE) & 0xFF;
            rx_msg[1] = (message_size)&0xFF;
            rx_msg += *modem_data.mt_message;
            unsigned int csum = goby::acomms::iridium::sbd_csum(*modem_data.mt_message);
            const int bits_in_byte = 8;
            rx_msg +=
                std::string(1, (csum & 0xFF00) >> bits_in_byte) + std::string(1, (csum & 0xFF));

            response = rx_msg;
            response += "\r\n\r\nOK";
        }
    }
    else if (command.substr(0, cier.size()) == cier) // RSSI / link available reporting
    {
        size_t equal_pos = command.find('=');
        if (equal_pos != std::string::npos)
        {
            try
            {
                std::string params_str = command.substr(equal_pos + 1);
                size_t pos = 0;
                std::vector<bool> params;

                while (true)
                {
                    size_t comma_pos = params_str.find(',', pos);
                    if (comma_pos == std::string::npos)
                    {
                        // Get last parameter
                        params.push_back(params_str.substr(pos) == "1");
                        break;
                    }
                    else
                    {
                        params.push_back(params_str.substr(pos, comma_pos - pos) == "1");
                        pos = comma_pos + 1;
                    }
                }

                enum
                {
                    ENABLE_CIEV = 0,
                    ENABLE_RSSI = 1,
                    ENABLE_SVCIND = 2
                };

                if (params.at(ENABLE_CIEV))
                {
                    if (params.size() > ENABLE_RSSI)
                        ciev_state_.rssi = params[ENABLE_RSSI];
                    if (params.size() > ENABLE_SVCIND)
                        ciev_state_.svcind = params[ENABLE_SVCIND];
                }
                else
                {
                    ciev_state_ = CIEVState();
                }
            }
            catch (const std::exception& e)
            {
                response = "ERROR";
            }
        }
        else
        {
            response = "ERROR";
        }
    }

    send_response(io.index(), command, response);
}

void goby::apps::acomms::RockBLOCKSimulator::loop()
{
    for (const auto& p : modem_data_)
    {
        auto index = p.first;
        if (ciev_state_.rssi)
        {
            send_response(index, "", "+CIEV:0,5");
        }
        if (ciev_state_.svcind)
        {
            send_response(index, "", "+CIEV:1,1");
        }
    }
}

void goby::apps::acomms::RockBLOCKSimulator::send_response(int index, const std::string& command,
                                                           const std::string& response)
{
    auto io_response = std::make_shared<goby::middleware::protobuf::IOData>();
    io_response->set_index(index);

    io_response->set_data(response + "\r\n");

    interthread().publish<pty_out>(io_response);
}

void goby::apps::acomms::RockBLOCKSimulator::write_mo_message(int index)
{
    auto& modem_data = modem_data_[index];
    auto modem_id = id_to_pty_index_.right.find(index)->second;

    httplib::Client client(cfg().mo_http_server());
    nlohmann::json jdata;
    jdata["momsn"] = modem_data.momsn;
    jdata["imei"] = imei_to_id_.right.at(modem_id);
    jdata["data"] = goby::util::hex_encode(*modem_data.mo_message);
    auto res = client.Post(cfg().mo_http_endpoint(), jdata.dump(), "application/json");
    if (res)
    {
        glog.is_debug1() && glog << group("http") << "Received HTTP result: " << res->status
                                 << std::endl;
        if (res->status == 200)
        {
            glog.is_verbose() && glog << group("http") << "Message success" << std::endl;
        }
        else
        {
            glog.is_warn() && glog << group("http") << "HTTP result not 200" << std::endl;
        }
    }
    else
    {
        auto err = res.error();
        glog.is_warn() && glog << group("http") << "HTTP error: " << httplib::to_string(err)
                               << std::endl;
    }
}

//
// RockBLOCKMTHTTPEndpointThread
//

goby::apps::acomms::RockBLOCKMTHTTPEndpointThread::RockBLOCKMTHTTPEndpointThread(
    const protobuf::RockBLOCKSimulatorConfig cfg)
    : goby::middleware::SimpleThread<protobuf::RockBLOCKSimulatorConfig>(cfg)
{
    for (const auto& imei_to_id : cfg.imei_to_id()) imei_in_use_.insert(imei_to_id.imei());

    // HTTP
    httplib::Server svr;

    svr.Post(
        "/rockblock/MT",
        [this](const httplib::Request& req, httplib::Response& res)
        {
            goby::acomms::iridium::protobuf::RockblockTransmit::Error err(
                goby::acomms::iridium::protobuf::RockblockTransmit::ERROR_SUCCESS);

            nlohmann::json mt_jdata;
            if (req.has_param("imei"))
            {
                mt_jdata["imei"] = req.get_param_value("imei");
                if (!imei_in_use_.count(mt_jdata["imei"]))
                    err = goby::acomms::iridium::protobuf::RockblockTransmit::
                        ROCKBLOCK_ERROR_IMEI_NOT_FOUND_ON_YOUR_ACCOUNT;
            }
            else
            {
                err = goby::acomms::iridium::protobuf::RockblockTransmit::
                    ROCKBLOCK_ERROR_IMEI_NOT_FOUND_ON_YOUR_ACCOUNT;
            }

            if (req.has_param("data"))
            {
                mt_jdata["data"] = req.get_param_value("data");
            }
            else
            {
                err = goby::acomms::iridium::protobuf::RockblockTransmit::ROCKBLOCK_ERROR_NO_DATA;
            }

            mt_jdata["mtmsn"] = mtmsn_;

            if (err == goby::acomms::iridium::protobuf::RockblockTransmit::ERROR_SUCCESS)
            {
                interthread().publish<mtdata>(mt_jdata);
                res.set_content("OK," + std::to_string(mtmsn_), "text/plain");
                ++mtmsn_;
            }
            else
            {
                res.set_content(
                    "FAILED," + std::to_string(static_cast<int>(err)) + "," +
                        goby::acomms::iridium::protobuf::RockblockTransmit::Error_Name(err),
                    "text/plain");
            }
        });

    glog.is_verbose() && glog << group("http")
                              << "Starting server on 0.0.0.0:" << cfg.mt_http_server_port()
                              << std::endl;
    svr.listen("0.0.0.0", cfg.mt_http_server_port());
}

//
// RockBLOCKPTYThread
//

void goby::apps::acomms::RockBLOCKPTYThread::async_read()
{
    auto read_handler = [this](const boost::system::error_code& ec, std::size_t bytes_transferred)
    {
        if (!ec && bytes_transferred > 0)
        {
            std::string bytes(bytes_transferred, 0);
            std::istream is(&buffer_);
            is.read(&bytes[0], bytes_transferred);

            if (mo_pending_write_size_ == 0)
            {
                // check for special case read in normal command mode
                // when we switch away from line based comms
                const std::string write_buffer = "AT+SBDWB";

                auto command = boost::trim_copy(bytes);
                if (command.substr(0, write_buffer.size()) == write_buffer)
                {
                    mo_pending_write_size_ =
                        goby::util::as<int>(command.substr(command.find('=') + 1)) +
                        SBD_CHECKSUM_BYTES;
                }
            }
            else
            {
                // reset after single AT+SBDWB read
                mo_pending_write_size_ = 0;
            }

            this->handle_read_success(bytes_transferred, bytes);
            this->async_read();
        }
        else
        {
            this->handle_read_error(ec);
        }
    };

    if (mo_pending_write_size_ == 0) // normal line based
        boost::asio::async_read_until(this->mutable_socket(), buffer_, "\r", read_handler);
    else // just after AT+SBDWB
        boost::asio::async_read(this->mutable_socket(), buffer_,
                                boost::asio::transfer_exactly(mo_pending_write_size_),
                                read_handler);
}
