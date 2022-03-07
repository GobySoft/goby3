// Copyright 2017-2021:
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

#include "goby/middleware/log.h"
#include "goby/middleware/log/dccl_log_plugin.h"
#include "goby/middleware/log/protobuf_log_plugin.h"
#include "goby/middleware/marshalling/interface.h"
#include "goby/util/debug_logger.h"

#include "goby/test/middleware/log/test.pb.h"

#if GOOGLE_PROTOBUF_VERSION < 3001000
#define ByteSizeLong ByteSize
#endif

using goby::middleware::log::LogEntry;
using goby::test::middleware::protobuf::CTDSample;
using goby::test::middleware::protobuf::TempSample;

constexpr goby::middleware::Group tempgroup("groups::temp");
constexpr goby::middleware::Group ctdgroup("groups::ctd");
int nctd = 6;

dccl::Codec codec;

goby::time::SystemClock::time_point start_time{goby::time::SystemClock::now()};

void read_log(int test, int version)
{
    goby::middleware::log::ProtobufPlugin pb_plugin;
    goby::middleware::log::DCCLPlugin dccl_plugin;
    LogEntry::reset();
    dccl::DynamicProtobufManager::reset();

    // can't read version since we corrupted it
    if (test == 1 && version < LogEntry::compiled_current_version)
        LogEntry::set_current_version(version);

    LogEntry::new_type_hook[goby::middleware::MarshallingScheme::DCCL] =
        [&](const std::string& type) {
            std::cout << "New type hook for DCCL: " << type << std::endl;
            assert(type == "goby.test.middleware.protobuf.CTDSample");
        };
    LogEntry::new_type_hook[goby::middleware::MarshallingScheme::PROTOBUF] =
        [&](const std::string& type) {
            std::cout << "New type hook for PROTOBUF: " << type << std::endl;
            assert(type == "goby.test.middleware.protobuf.TempSample" ||
                   type == "google.protobuf.FileDescriptorProto");
        };

    std::ifstream in_log_file("/tmp/goby3_test_log.goby");
    pb_plugin.register_read_hooks(in_log_file);
    dccl_plugin.register_read_hooks(in_log_file);

    try
    {
        LogEntry entry;
        entry.parse(&in_log_file);
        assert(test != 3 && test != 4 && test != 5 && test != 6);
        assert(entry.scheme() == goby::middleware::MarshallingScheme::PROTOBUF);
        assert(entry.group() == tempgroup);
        assert(entry.type() == TempSample::descriptor()->full_name());

        if (version >= 3)
            assert(entry.timestamp() == start_time);

        auto temp_samples = pb_plugin.parse_message(entry);
        assert(temp_samples.size() == 1 && temp_samples[0]);
        auto& t = dynamic_cast<TempSample&>(*temp_samples[0]);
        assert(t.temperature() == 500);
    }
    catch (goby::middleware::log::LogException& e)
    {
        std::cerr << e.what() << std::endl;
        assert(test == 3 || test == 4 || test == 5 || test == 6);
    }

    if (test == 4)
    {
        LogEntry entry;
        entry.parse(&in_log_file);
        assert(entry.scheme() == goby::middleware::MarshallingScheme::PROTOBUF);
        // corrupted index
        assert(entry.group() == "_unknown1_");
        assert(entry.type() == TempSample::descriptor()->full_name());
    }

    for (int i = 0; i < nctd / 2; ++i)
    {
        LogEntry entry;
        entry.parse(&in_log_file);

        assert(entry.scheme() == goby::middleware::MarshallingScheme::DCCL);
        assert(entry.group() == ctdgroup);
        assert(entry.type() == CTDSample::descriptor()->full_name());
        if (version >= 3)
            assert(entry.timestamp() == start_time + std::chrono::seconds(1));

        auto ctd_samples = dccl_plugin.parse_message(entry);
        assert(ctd_samples.size() == 2 && ctd_samples[0] && ctd_samples[1]);

        auto& ctd1 = dynamic_cast<CTDSample&>(*ctd_samples[0]);
        auto& ctd2 = dynamic_cast<CTDSample&>(*ctd_samples[1]);
        assert(ctd1.temperature() == i * 2 + 5);
        assert(ctd2.temperature() == (i * 2 + 1) + 5);
    }

    // eof
    try
    {
        LogEntry entry;
        entry.parse(&in_log_file);
        bool expected_eof = false;
        assert(expected_eof);
    }
    catch (std::ifstream::failure& e)
    {
        assert(in_log_file.eof());
    }
}

void write_log(int test, int version)
{
    goby::middleware::log::ProtobufPlugin pb_plugin;
    goby::middleware::log::DCCLPlugin dccl_plugin;
    LogEntry::reset();
    LogEntry::set_current_version(version);
    std::ofstream out_log_file("/tmp/goby3_test_log.goby");
    pb_plugin.register_write_hooks(out_log_file);
    dccl_plugin.register_write_hooks(out_log_file);

    switch (test)
    {
        default: break;
        case 1:
            // insert some chars at the beginning of the file (longer than version byte string)
            out_log_file << "foooo";
            break;
    }

    TempSample t;
    {
        t.set_temperature(500);
        std::vector<unsigned char> data(t.ByteSizeLong());
        t.SerializeToArray(&data[0], data.size());
        LogEntry entry(data, goby::middleware::MarshallingScheme::PROTOBUF,
                       TempSample::descriptor()->full_name(), tempgroup, start_time);
        entry.serialize(&out_log_file);
    }

    switch (test)
    {
        default: break;
        case 2:
            // insert some chars at the middle of the file
            out_log_file << "bar";
            break;
        case 3:
        {
            // corrupt the previous entry
            auto pos = out_log_file.tellp();
            out_log_file.seekp(pos - std::streamoff(LogEntry::crc_bytes_ + 2));
            out_log_file.put(0);
            out_log_file.seekp(pos);
            break;
        }

        case 4:
        {
            // corrupt the start (index) data
            auto pos = out_log_file.tellp();
            auto version_bytes = LogEntry::version_bytes_;
            if (version == 1)
                version_bytes = 0;
            out_log_file.seekp(version_bytes + LogEntry::magic_bytes_ + LogEntry::size_bytes_ +
                                   LogEntry::scheme_bytes_ + LogEntry::group_bytes_ - 1,
                               out_log_file.beg);
            out_log_file.put(0);
            out_log_file.seekp(pos);
            break;
        }

        case 5:
        {
            // corrupt the size field to make it larger than the file
            auto pos = out_log_file.tellp();

            out_log_file.seekp(pos -
                               std::streamoff(LogEntry::crc_bytes_ + t.ByteSizeLong() +
                                              LogEntry::type_bytes_ + LogEntry::group_bytes_ +
                                              LogEntry::scheme_bytes_ + LogEntry::size_bytes_ - 1));
            out_log_file.put(0xFF);
            out_log_file.seekp(pos);
        }

        case 6:
        {
            // corrupt the size field to make it just larger than the message
            auto pos = out_log_file.tellp();

            out_log_file.seekp(pos - std::streamoff(LogEntry::crc_bytes_ + t.ByteSizeLong() +
                                                    LogEntry::type_bytes_ + LogEntry::group_bytes_ +
                                                    LogEntry::scheme_bytes_ + 1));
            out_log_file.put(0x14);
            out_log_file.seekp(pos);
        }
    }

    std::vector<CTDSample> ctds;
    for (int i = 0; i < nctd; i += 2)
    {
        CTDSample ctd1;
        ctd1.set_temperature(i + 5);
        CTDSample ctd2;
        ctd2.set_temperature(i + 1 + 5);

        std::string encoded1, encoded2;
        codec.encode(&encoded1, ctd1);
        codec.encode(&encoded2, ctd2);

        std::string encoded = encoded1 + encoded2;

        std::vector<unsigned char> data(encoded.begin(), encoded.end());
        LogEntry entry(data, goby::middleware::MarshallingScheme::DCCL,
                       CTDSample::descriptor()->full_name(), ctdgroup,
                       start_time + std::chrono::seconds(1));
        entry.serialize(&out_log_file);
        ctds.push_back(ctd1);
        ctds.push_back(ctd2);
    }
}

int main(int /*argc*/, char* argv[])
{
    goby::glog.add_stream(goby::util::logger::DEBUG3, &std::cerr);
    goby::glog.set_name(argv[0]);

    codec.load<CTDSample>();

    int ntests = 7;
    int nversions = 3;

    for (int version = 1; version <= nversions; ++version)
    {
        for (int test = 0; test < ntests; ++test)
        {
            std::cout << "Running test " << test << ", log version: " << version << std::endl;
            write_log(test, version);
            read_log(test, version);
        }
    }

    std::cout << "all tests passed" << std::endl;
}
