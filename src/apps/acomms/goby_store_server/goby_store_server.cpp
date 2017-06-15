// Copyright 2009-2017 Toby Schneider (http://gobysoft.org/index.wt/people/toby)
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

#include "goby/acomms/acomms_constants.h"
#include "goby/common/time.h"
#include "goby/acomms/protobuf/store_server.pb.h"
#include "goby/common/zeromq_application_base.h"
#include "goby/pb/protobuf_node.h"
#include "goby_store_server_config.pb.h"
#include "goby/util/binary.h"

using namespace goby::common::logger;
using goby::common::goby_time;

namespace goby
{
    namespace acomms
    {
        class GobyStoreServer : public goby::common::ZeroMQApplicationBase,
                                public goby::pb::StaticProtobufNode
        {
        public:
            GobyStoreServer(protobuf::GobyStoreServerConfig* cfg);
            ~GobyStoreServer()
                { if(db_) sqlite3_close(db_); }
            

        private:
            void handle_request(const protobuf::StoreServerRequest& request);
            void loop();

            void check(int rc, const std::string& error_prefix);

        private:
            static goby::common::ZeroMQService zeromq_service_;
            protobuf::GobyStoreServerConfig& cfg_;
            sqlite3* db_;

            // maps modem_id to time (microsecs since UNIX)
            std::map<int, uint64> last_request_time_;
        };
    }
}

goby::common::ZeroMQService goby::acomms::GobyStoreServer::zeromq_service_;


int main(int argc, char* argv[])
{
    goby::acomms::protobuf::GobyStoreServerConfig cfg;
    goby::run<goby::acomms::GobyStoreServer>(argc, argv, &cfg);
}

goby::acomms::GobyStoreServer::GobyStoreServer(protobuf::GobyStoreServerConfig* cfg)
    : ZeroMQApplicationBase(&zeromq_service_, cfg),
      StaticProtobufNode(&zeromq_service_),
      cfg_(*cfg),
      db_(0)
{
    // create database
    if(!boost::filesystem::exists(cfg_.db_file_dir()))
        throw(goby::Exception("db_file_dir does not exist: " + cfg_.db_file_dir()));

    std::string full_db_name = cfg_.db_file_dir() + "/";
    if(cfg_.has_db_file_name())
        full_db_name += cfg_.db_file_name();
    else
        full_db_name += "goby_store_server_" + goby::common::goby_file_timestamp() + ".db";

    int rc;    
    rc = sqlite3_open(full_db_name.c_str(), &db_);
    if(rc)
        throw(goby::Exception("Can't open database: " + std::string(sqlite3_errmsg(db_))));
    
    // initial tables
    char* errmsg;
    rc = sqlite3_exec(db_, "CREATE TABLE IF NOT EXISTS ModemTransmission (id INTEGER PRIMARY KEY ASC AUTOINCREMENT, src INTEGER, dest INTEGER, microtime INTEGER, bytes BLOB);", 0, 0, &errmsg);

    if (rc != SQLITE_OK)
    {
        std::string error(errmsg);
        sqlite3_free(errmsg);

        throw(goby::Exception("SQL error: " + error));
    }
    
    // set up receiving requests    
    on_receipt<protobuf::StoreServerRequest>(
        cfg_.reply_socket().socket_id(), &GobyStoreServer::handle_request, this);

    // start zeromqservice
    common::protobuf::ZeroMQServiceConfig service_cfg;
    service_cfg.add_socket()->CopyFrom(cfg_.reply_socket());
    zeromq_service_.set_cfg(service_cfg);
}

void goby::acomms::GobyStoreServer::loop()
{
}

void goby::acomms::GobyStoreServer::handle_request(const protobuf::StoreServerRequest& request)
{
    glog.is(DEBUG1) && glog << "Got request: " << request.DebugString() << std::endl;

    uint64 request_time = goby_time<uint64>();
    
    protobuf::StoreServerResponse response;
    response.set_modem_id(request.modem_id());

    // insert any rows into the table
    for(int i = 0, n = request.outbox_size(); i < n; ++i)
    {
        glog.is(DEBUG1) && glog << "Trying to insert (size: " << request.outbox(i).ByteSize() << "): " << request.outbox(i).DebugString() << std::endl;
        
        sqlite3_stmt* insert;        
        
        check(sqlite3_prepare(db_, "INSERT INTO ModemTransmission (src, dest, microtime, bytes) VALUES (?, ?, ?, ?);", -1, &insert, 0),
              "Insert statement preparation failed");
        check(sqlite3_bind_int(insert, 1, request.outbox(i).src()),
              "Insert `src` binding failed");
        check(sqlite3_bind_int(insert, 2, request.outbox(i).dest()),
              "Insert `dest` binding failed");
        check(sqlite3_bind_int64(insert, 3, goby_time<uint64>()),
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

    if(!last_request_time_.count(request.modem_id()))
        last_request_time_.insert(std::make_pair(request.modem_id(), 0));
    
    sqlite3_stmt* select;
    check(sqlite3_prepare(db_, "SELECT bytes FROM ModemTransmission WHERE src != ?1 AND (microtime > ?2 AND microtime <= ?3 );", -1, &select, 0),
          "Select statement preparation failed");

    check(sqlite3_bind_int(select, 1, request.modem_id()),
          "Select request modem_id binding failed");
    check(sqlite3_bind_int64(select, 2, last_request_time_[request.modem_id()]),
          "Select `microtime` last time binding failed");
    check(sqlite3_bind_int64(select, 3, request_time),
          "Select `microtime` this time binding failed");
    

    int rc = sqlite3_step(select);
    while(rc == SQLITE_ROW)
    {
        switch(rc)
        {
            case SQLITE_ROW:
            {
                const unsigned char* bytes = sqlite3_column_text(select, 0);
                int num_bytes = sqlite3_column_bytes(select, 0);

                // std::string byte_string(reinterpret_cast<const char*>(bytes), num_bytes);
            
                // glog.is(DEBUG1) && glog << "Bytes (hex): " << goby::util::hex_encode(byte_string) << std::endl;
            
                response.add_inbox()->ParseFromArray(bytes, num_bytes);
                glog.is(DEBUG1) &&
                    glog << "Got message for inbox (size: " << num_bytes << "): " << response.inbox(response.inbox_size()-1).DebugString()
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
    
    send(response, cfg_.reply_socket().socket_id());
}


void goby::acomms::GobyStoreServer::check(int rc, const std::string& error_prefix)
{
    if(rc != SQLITE_OK && rc != SQLITE_DONE)
        throw(goby::Exception(error_prefix + ": " + std::string(sqlite3_errmsg(db_))));
}

