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

#include <sqlite3.h>

#include <boost/filesystem.hpp>

#include "goby/middleware/marshalling/protobuf.h"

#include "goby/acomms/acomms_constants.h"
#include "goby/acomms/modemdriver/store_server_driver.h"
#include "goby/acomms/protobuf/store_server.pb.h"
#include "goby/acomms/protobuf/store_server_config.pb.h"
#include "goby/middleware/acomms/groups.h"
#include "goby/middleware/application/multi_thread.h"
#include "goby/middleware/io/line_based/tcp_server.h"
#include "goby/time/convert.h"
#include "goby/time/system_clock.h"
#include "goby/util/binary.h"

#if GOOGLE_PROTOBUF_VERSION < 3001000
#define ByteSizeLong ByteSize
#endif

using goby::glog;
using namespace goby::util::logger;

constexpr goby::middleware::Group tcp_server_in{"tcp_server_in"};
constexpr goby::middleware::Group tcp_server_out{"tcp_server_out"};

namespace goby
{
namespace apps
{
namespace acomms
{

class StoreServerConfigurator
    : public goby::middleware::ProtobufConfigurator<protobuf::StoreServerConfig>
{
  public:
    StoreServerConfigurator(int argc, char* argv[])
        : goby::middleware::ProtobufConfigurator<protobuf::StoreServerConfig>(argc, argv)
    {
        protobuf::StoreServerConfig& cfg = mutable_cfg();
        cfg.mutable_tcp_server()->set_end_of_line(goby::acomms::StoreServerDriver::eol);
        if (!cfg.tcp_server().has_bind_port())
        {
            cfg.mutable_tcp_server()->set_bind_port(goby::acomms::StoreServerDriver::default_port);
        }
    }
};

class StoreServer : public goby::middleware::MultiThreadStandaloneApplication<
                        goby::apps::acomms::protobuf::StoreServerConfig>
{
  public:
    StoreServer();
    ~StoreServer()
    {
        if (db_)
            sqlite3_close(db_);
    }

  private:
    void handle_request(const goby::middleware::protobuf::TCPEndPoint& tcp_src,
                        const goby::acomms::protobuf::StoreServerRequest& request);

    void check(int rc, const std::string& error_prefix);

  private:
    sqlite3* db_;

    // maps modem_id to time (microsecs since UNIX)
    std::map<int, std::uint64_t> last_request_time_;
};
} // namespace acomms
} // namespace apps
} // namespace goby

int main(int argc, char* argv[])
{
    goby::run<goby::apps::acomms::StoreServer>(
        goby::apps::acomms::StoreServerConfigurator(argc, argv));
}

goby::apps::acomms::StoreServer::StoreServer() : db_(0)
{
    // create database
    if (!boost::filesystem::exists(cfg().db_file_dir()))
        throw(goby::Exception("db_file_dir does not exist: " + cfg().db_file_dir()));

    std::string full_db_name = cfg().db_file_dir() + "/";
    if (cfg().has_db_file_name())
        full_db_name += cfg().db_file_name();
    else
        full_db_name += "goby_store_server_" + goby::time::file_str() + ".db";

    int rc;
    rc = sqlite3_open(full_db_name.c_str(), &db_);
    if (rc)
        throw(goby::Exception("Can't open database: " + std::string(sqlite3_errmsg(db_))));

    // initial tables
    char* errmsg;
    rc = sqlite3_exec(db_,
                      "CREATE TABLE IF NOT EXISTS ModemTransmission (id INTEGER PRIMARY KEY ASC "
                      "AUTOINCREMENT, src INTEGER, dest INTEGER, microtime INTEGER, bytes BLOB);",
                      0, 0, &errmsg);

    if (rc != SQLITE_OK)
    {
        std::string error(errmsg);
        sqlite3_free(errmsg);

        throw(goby::Exception("SQL error: " + error));
    }

    // subscribe to events from server thread
    interthread().subscribe<tcp_server_in>(
        [this](const goby::middleware::protobuf::TCPServerEvent& event) {
            glog.is_verbose() && glog << "Got TCP event: " << event.ShortDebugString() << std::endl;
        });

    // subscribe to incoming data from server thread
    interthread().subscribe<tcp_server_in>(
        [this](const goby::middleware::protobuf::IOData& tcp_data_in)
        {
            try
            {
                goby::acomms::protobuf::StoreServerRequest request;
                goby::acomms::StoreServerDriver::parse_store_server_message(tcp_data_in.data(),
                                                                            &request);
                handle_request(tcp_data_in.tcp_src(), request);
            }
            catch (const std::exception& e)
            {
                glog.is_warn() && glog << "Failed to parse/handle incoming request: " << e.what()
                                       << std::endl;
            }
        });

    using TCPServerThread =
        goby::middleware::io::TCPServerThreadLineBased<tcp_server_in, tcp_server_out>;

    launch_thread<TCPServerThread>(cfg().tcp_server());
}

void goby::apps::acomms::StoreServer::handle_request(
    const goby::middleware::protobuf::TCPEndPoint& tcp_src,
    const goby::acomms::protobuf::StoreServerRequest& request)
{
    glog.is(DEBUG1) && glog << "Got request: " << request.DebugString() << std::endl;

    std::uint64_t request_time = goby::time::SystemClock::now<goby::time::MicroTime>().value();

    goby::acomms::protobuf::StoreServerResponse response;
    response.set_modem_id(request.modem_id());

    // insert any rows into the table
    for (int i = 0, n = request.outbox_size(); i < n; ++i)
    {
        glog.is(DEBUG1) && glog << "Trying to insert (size: " << request.outbox(i).ByteSizeLong()
                                << "): " << request.outbox(i).DebugString() << std::endl;

        sqlite3_stmt* insert;

        check(
            sqlite3_prepare(
                db_,
                "INSERT INTO ModemTransmission (src, dest, microtime, bytes) VALUES (?, ?, ?, ?);",
                -1, &insert, 0),
            "Insert statement preparation failed");
        check(sqlite3_bind_int(insert, 1, request.outbox(i).src()), "Insert `src` binding failed");
        check(sqlite3_bind_int(insert, 2, request.outbox(i).dest()),
              "Insert `dest` binding failed");
        check(sqlite3_bind_int64(insert, 3,
                                 goby::time::SystemClock::now<goby::time::MicroTime>().value()),
              "Insert `microtime` binding failed");

        std::string bytes;
        request.outbox(i).SerializeToString(&bytes);

        //        glog.is(DEBUG1) && glog << "Bytes (hex): " << goby::util::hex_encode(bytes) << std::endl;

        check(sqlite3_bind_blob(insert, 4, bytes.data(), bytes.size(), SQLITE_STATIC),
              "Insert `bytes` binding failed");

        check(sqlite3_step(insert), "Insert step failed");
        check(sqlite3_finalize(insert), "Insert statement finalize failed");

        glog.is(DEBUG1) && glog << "Insert successful." << std::endl;
    }

    // find any rows to respond with
    glog.is(DEBUG1) && glog << "Trying to select for dest: " << request.modem_id() << std::endl;

    if (!last_request_time_.count(request.modem_id()))
        last_request_time_.insert(std::make_pair(request.modem_id(), 0));

    sqlite3_stmt* select;
    check(sqlite3_prepare(db_,
                          "SELECT bytes FROM ModemTransmission WHERE src != ?1 AND (microtime > ?2 "
                          "AND microtime <= ?3 );",
                          -1, &select, 0),
          "Select statement preparation failed");

    check(sqlite3_bind_int(select, 1, request.modem_id()),
          "Select request modem_id binding failed");
    check(sqlite3_bind_int64(select, 2, last_request_time_[request.modem_id()]),
          "Select `microtime` last time binding failed");
    check(sqlite3_bind_int64(select, 3, request_time),
          "Select `microtime` this time binding failed");

    int rc = sqlite3_step(select);
    while (rc == SQLITE_ROW)
    {
        switch (rc)
        {
            case SQLITE_ROW:
            {
                const unsigned char* bytes = sqlite3_column_text(select, 0);
                int num_bytes = sqlite3_column_bytes(select, 0);

                // std::string byte_string(reinterpret_cast<const char*>(bytes), num_bytes);

                // glog.is(DEBUG1) && glog << "Bytes (hex): " << goby::util::hex_encode(byte_string) << std::endl;

                response.add_inbox()->ParseFromArray(bytes, num_bytes);
                glog.is(DEBUG1) && glog << "Got message for inbox (size: " << num_bytes << "): "
                                        << response.inbox(response.inbox_size() - 1).DebugString()
                                        << std::endl;
                rc = sqlite3_step(select);
            }
            break;

            default: check(rc, "Select step failed"); break;
        }
    }

    check(sqlite3_finalize(select), "Select statement finalize failed");
    glog.is(DEBUG1) && glog << "Select successful." << std::endl;

    last_request_time_[request.modem_id()] = request_time;

    goby::middleware::protobuf::IOData tcp_data_out;
    *tcp_data_out.mutable_tcp_dest() = tcp_src;

    try
    {
        goby::acomms::StoreServerDriver::serialize_store_server_message(
            response, tcp_data_out.mutable_data());
        interthread().publish<tcp_server_out>(tcp_data_out);
    }
    catch (const std::exception& e)
    {
        glog.is_warn() && glog << "Failed to serialize outgoing response: " << e.what()
                               << std::endl;
    }
}

void goby::apps::acomms::StoreServer::check(int rc, const std::string& error_prefix)
{
    if (rc != SQLITE_OK && rc != SQLITE_DONE)
        throw(goby::Exception(error_prefix + ": " + std::string(sqlite3_errmsg(db_))));
}
