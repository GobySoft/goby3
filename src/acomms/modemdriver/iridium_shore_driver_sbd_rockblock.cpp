// Copyright 2015-2023:
//   GobySoft, LLC (2013-)
//   Community contributors (see AUTHORS file)
// File authors:
//   Toby Schneider <toby@gobysoft.org>
//
//
// This file is part of the Goby Underwater Autonomy Project Libraries
// ("The Goby Libraries").
//
// The Goby Libraries are free software: you can redistribute them and/or modify
// them under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 2.1 of the License, or
// (at your option) any later version.
//
// The Goby Libraries are distributed in the hope that they will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with Goby.  If not, see <http://www.gnu.org/licenses/>.

#include "goby/util/as.h"                               // for as
#include "goby/util/binary.h"                           // for hex_e...
#include "goby/util/debug_logger/flex_ostream.h"        // for opera...
#include "goby/util/debug_logger/flex_ostreambuf.h"     // for DEBUG1
#include "goby/util/debug_logger/logger_manipulators.h" // for opera...

#include "goby/util/thirdparty/jwt-cpp/traits/nlohmann-json/defaults.h"

#include "goby/util/thirdparty/jwt-cpp/jwt.h"

#include "iridium_shore_driver.h"

using namespace goby::util::logger;
using goby::glog;

// from https://docs.rock7.com/reference/push-api
const std::string rockblock_rsa_pubkey = R"(-----BEGIN PUBLIC KEY-----
MIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEAlaWAVJfNWC4XfnRx96p9cztBcdQV6l8aKmzAlZdpEcQR6MSPzlgvihaUHNJgKm8t5ShR3jcDXIOI7er30cIN4/9aVFMe0LWZClUGgCSLc3rrMD4FzgOJ4ibD8scVyER/sirRzf5/dswJedEiMte1ElMQy2M6IWBACry9u12kIqG0HrhaQOzc6Tr8pHUWTKft3xwGpxCkV+K1N+9HCKFccbwb8okRP6FFAMm5sBbw4yAu39IVvcSL43Tucaa79FzOmfGs5mMvQfvO1ua7cOLKfAwkhxEjirC0/RYX7Wio5yL6jmykAHJqFG2HT0uyjjrQWMtoGgwv9cIcI7xbsDX6owIDAQAB
-----END PUBLIC KEY-----)";

void goby::acomms::IridiumShoreDriver::startup_sbd_rockblock(const protobuf::DriverConfig& cfg)
{
    if (!iridium_shore_driver_cfg().has_rockblock())
        glog.is(DIE) && glog << group(glog_out_group())
                             << "Must specify rockblock {} configuration when using SBD_ROCKBLOCK"
                             << std::endl;

    // use the build-in modem connection for receiving MO messages as HTTP is line-based
    driver_cfg_.set_connection_type(goby::acomms::protobuf::DriverConfig::CONNECTION_TCP_AS_SERVER);

    if (iridium_shore_driver_cfg().has_mo_sbd_server_port() || !driver_cfg_.has_tcp_port())
        driver_cfg_.set_tcp_port(iridium_shore_driver_cfg().mo_sbd_server_port());

    // new line for HTTP; JSON message has no newline so use }
    driver_cfg_.set_line_delimiter("\n|}");
}

// e.g.,

// POST / HTTP/1.1
// User-Agent: Rock7PushApi
// Content-Type: application/json; charset=utf-8
// Content-Length: 1125
// Host: gobysoft.org:8080
// Connection: Keep-Alive
// Accept-Encoding: gzip

// {"momsn":66,"data":"546865726520617265203130207479706573206f662070656f706c652077686f20756e6465727374616e642062696e617279","serial":14331,"iridium_latitude":75.0001,"iridium_cep":3.0,"JWT":"eyJhbGciOiJSUzI1NiJ9.eyJpc3MiOiJSb2NrIDciLCJpYXQiOjE2OTM4ODg0NzUsImRhdGEiOiI1NDY4NjU3MjY1MjA2MTcyNjUyMDMxMzAyMDc0Nzk3MDY1NzMyMDZmNjYyMDcwNjU2ZjcwNmM2NTIwNzc2ODZmMjA3NTZlNjQ2NTcyNzM3NDYxNmU2NDIwNjI2OTZlNjE3Mjc5IiwiZGV2aWNlX3R5cGUiOiJST0NLQkxPQ0siLCJpbWVpIjoiMzAwNDM0MDYyMDk5NDMwIiwiaXJpZGl1bV9jZXAiOiIzLjAiLCJpcmlkaXVtX2xhdGl0dWRlIjoiNzUuMDAwMSIsImlyaWRpdW1fbG9uZ2l0dWRlIjoiMTU0LjEyMDkiLCJtb21zbiI6IjY2Iiwic2VyaWFsIjoiMTQzMzEiLCJ0cmFuc21pdF90aW1lIjoiMjMtMDktMDUgMDQ6MzM6MjQifQ.XfBpmA_XmkRoK1PPBjYKm-cQzEXIV-OOjt5ODgF6TX5LMhoDlDkAA7JrFiRnCY8zYMzseLn368oBwNR01yIe8bYVIVM5MM0ykkovRve-ubmwdHYvH0Hgiph-a69yeMwzvGOyfW8WFxTJwg8I_UCCWyFwG_FIHk-iZsiAZ42h7AYDk8zpMStH3-jD5I8ymGQXRe9b-QfLlAOFfXMNBKuFRBsMw1Jgbgv5X5ScpkYrVMozOkjMFaLPSclYeE1jodlv_QUVXR-4g_Xczmdff56HnOfBTVuUwdFkr661r5EoXgX3GB-natzZ5XZ-dy8j84GQYOZi5wBmtFalKdJdmHS1rg","imei":"300434062099430","device_type":"ROCKBLOCK","transmit_time":"23-09-05 04:33:24","iridium_longitude":154.1209}
void goby::acomms::IridiumShoreDriver::receive_sbd_mo_rockblock()
{
    std::string line;
    const std::string start = "POST / HTTP/1.1";
    while (modem_read(&line))
    {
        if (boost::trim_copy(line) == start)
        {
            if (rb_msg_ && rb_msg_->state != RockblockHTTPMessage::MessageState::COMPLETE)
            {
                glog.is_warn() &&
                    glog << group(glog_in_group())
                         << "Received start of new HTTP message without completing last message"
                         << std::endl;
            }

            rb_msg_.reset(new RockblockHTTPMessage);
        }
        else if (rb_msg_)
        {
            if (rb_msg_->state == RockblockHTTPMessage::MessageState::COMPLETE)
            {
                glog.is_warn() && glog << group(glog_in_group())
                                       << "Received data after complete message, ignoring."
                                       << std::endl;
                continue;
            }
            else if (rb_msg_->state == RockblockHTTPMessage::MessageState::HEADER)
            {
                if (line == "\r\n")
                {
                    rb_msg_->state = RockblockHTTPMessage::MessageState::BODY;
                    for (const auto& h_p : rb_msg_->header)
                        glog.is_debug2() && glog << group(glog_in_group()) << "Header ["
                                                 << h_p.first << ":" << h_p.second << "]"
                                                 << std::endl;
                }
                else
                {
                    auto colon_pos = line.find(":");
                    if (colon_pos == std::string::npos)
                    {
                        glog.is_warn() && glog << group(glog_in_group())
                                               << "Received header field without colon, ignoring"
                                               << std::endl;
                        continue;
                    }
                    std::string key = line.substr(0, colon_pos);
                    std::string value = line.substr(colon_pos);

                    boost::trim_if(key, boost::is_any_of(": \r\n"));
                    boost::trim_if(value, boost::is_any_of(": \r\n"));
                    rb_msg_->header.insert(std::make_pair(key, value));
                }
            }
            else if (rb_msg_->state == RockblockHTTPMessage::MessageState::BODY)
            {
                rb_msg_->body += line;
                if (rb_msg_->header.count("Content-Length"))
                {
                    auto length = goby::util::as<int>(rb_msg_->header.at("Content-Length"));
                    if (rb_msg_->body.size() == length)
                    {
                        rb_msg_->state = RockblockHTTPMessage::MessageState::COMPLETE;

                        try
                        {
                            auto json_data = nlohmann::json::parse(rb_msg_->body);
                            glog.is_debug1() && glog << "Received valid JSON message: "
                                                     << json_data.dump(2) << std::endl;

                            auto verify = jwt::verify()
                                              .allow_algorithm(jwt::algorithm::rs256(
                                                  rockblock_rsa_pubkey, "", "", ""))
                                              .with_issuer("Rock 7");
                            auto decoded = jwt::decode(json_data["JWT"].get<std::string>());
                            try
                            {
                                verify.verify(decoded);
                                receive_sbd_mo_data(goby::util::hex_decode(json_data["data"]));
                            }
                            catch (const std::exception& e)
                            {
                                glog.is_warn() && glog << "Discarding message: could not verify "
                                                          "Rockblock JWT against public key: "
                                                       << e.what() << std::endl;
                            }

                            modem_write("HTTP/1.1 200 OK\r\n");
                            modem_write("Content-Length: 0\r\n");
                            modem_write("Connection: close\r\n\r\n");
                        }
                        catch (std::exception& e)
                        {
                            glog.is_warn() && glog << group(glog_in_group())
                                                   << "Failed to parse JSON: " << e.what()
                                                   << ", data: " << rb_msg_->body << std::endl;
                        }
                    }
                }
                else
                {
                    glog.is_warn() && glog << group(glog_in_group())
                                           << "No Content-Length in header" << std::endl;
                }
            }
        }
    }
}

void goby::acomms::IridiumShoreDriver::send_sbd_mt_rockblock(const std::string& bytes,
                                                             const std::string& imei)
{
}
