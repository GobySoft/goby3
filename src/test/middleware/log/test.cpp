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
#include "goby/middleware/log.h"
#include "goby/middleware/serialize_parse.h"

#include "test.pb.h"

constexpr goby::Group tempgroup("groups::temp");
constexpr goby::Group ctdgroup("groups::ctd");
int nctd = 5;

dccl::Codec codec;
std::set<const google::protobuf::FileDescriptor*> written_file_desc_;

constexpr goby::Group file_desc_group("goby::logger::ProtobufFileDescriptor");

void add_new_protobuf_type(int scheme, const std::string& protobuf_type,
                           std::ofstream& out_log_file);

void read_log(int test)
{
    goby::LogEntry::reset();

    goby::LogEntry::new_type_hook[goby::MarshallingScheme::DCCL] = [&](const std::string& type) {
        std::cout << "New type hook for DCCL: " << type << std::endl;
        assert(type == "CTDSample");
    };
    goby::LogEntry::new_type_hook[goby::MarshallingScheme::PROTOBUF] =
        [&](const std::string& type) {
            std::cout << "New type hook for PROTOBUF: " << type << std::endl;
            assert(type == "TempSample" || type == "google.protobuf.FileDescriptorProto");
        };

    goby::LogEntry::filter_hook[{
        static_cast<int>(goby::MarshallingScheme::PROTOBUF),
        static_cast<std::string>(file_desc_group),
        google::protobuf::FileDescriptorProto::descriptor()->full_name()}] =
        [](const std::vector<unsigned char>& data) {
            std::cout << "FileDescProto hook called with " << data.size() << " bytes of data"
                      << std::endl;
            google::protobuf::FileDescriptorProto file_desc_proto;
            file_desc_proto.ParseFromArray(&data[0], data.size());
            std::cout << "Adding: " << file_desc_proto.name() << std::endl;
            dccl::DynamicProtobufManager::add_protobuf_file(file_desc_proto);
        };

    std::ifstream in_log_file("/tmp/goby3_test_log.goby");
    try
    {
        goby::LogEntry entry;
        entry.parse(&in_log_file);
        assert(test != 3 && test != 4 && test != 5 && test != 6);
        assert(entry.scheme() == goby::MarshallingScheme::PROTOBUF);
        assert(entry.group() == tempgroup);
        assert(entry.type() == TempSample::descriptor()->full_name());

        TempSample t;
        const auto& data = entry.data();
        t.ParseFromArray(&data[0], data.size());
        assert(t.temperature() == 500);
    }
    catch (goby::Exception& e)
    {
        std::cerr << e.what() << std::endl;
        assert(test == 3 || test == 4 || test == 5 || test == 6);
    }

    if (test == 4)
    {
        goby::LogEntry entry;
        entry.parse(&in_log_file);
        assert(entry.scheme() == goby::MarshallingScheme::PROTOBUF);
        // corrupted index
        assert(entry.group() == "_unknown1_");
        assert(entry.type() == TempSample::descriptor()->full_name());
    }

    for (int i = 0; i < nctd; ++i)
    {
        goby::LogEntry entry;
        entry.parse(&in_log_file);

        assert(entry.scheme() == goby::MarshallingScheme::DCCL);
        assert(entry.group() == ctdgroup);
        assert(entry.type() == CTDSample::descriptor()->full_name());

        CTDSample ctd;
        const auto& data = entry.data();
        codec.decode(data.begin(), data.end(), &ctd);
        assert(ctd.temperature() == i + 5);
    }

    // eof
    try
    {
        goby::LogEntry entry;
        entry.parse(&in_log_file);
        bool expected_eof = false;
        assert(expected_eof);
    }
    catch (std::ifstream::failure e)
    {
        assert(in_log_file.eof());
    }
}

void write_log(int test)
{
    goby::LogEntry::reset();
    written_file_desc_.clear();

    std::ofstream out_log_file("/tmp/goby3_test_log.goby");

    goby::LogEntry::new_type_hook[goby::MarshallingScheme::DCCL] = [&](const std::string& type) {
        add_new_protobuf_type(goby::MarshallingScheme::DCCL, type, out_log_file);
    };

    goby::LogEntry::new_type_hook[goby::MarshallingScheme::PROTOBUF] =
        [&](const std::string& type) {
            add_new_protobuf_type(goby::MarshallingScheme::PROTOBUF, type, out_log_file);
        };

    switch (test)
    {
        default: break;
        case 1:
            // insert some chars at the beginning of the file
            out_log_file << "foo";
            break;
    }

    TempSample t;
    {
        t.set_temperature(500);
        std::vector<unsigned char> data(t.ByteSize());
        t.SerializeToArray(&data[0], data.size());
        goby::LogEntry entry(data, goby::MarshallingScheme::PROTOBUF,
                             TempSample::descriptor()->full_name(), tempgroup);
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
            out_log_file.seekp(pos - std::ios::streamoff(goby::LogEntry::crc_bytes_ + 2));
            out_log_file.put(0);
            out_log_file.seekp(pos);
            break;
        }

        case 4:
        {
            // corrupt the start (index)
            auto pos = out_log_file.tellp();
            out_log_file.seekp(7, out_log_file.beg);
            out_log_file.put(0);
            out_log_file.seekp(pos);
            break;
        }

        case 5:
        {
            // corrupt the size field to make it larger than the file
            auto pos = out_log_file.tellp();

            out_log_file.seekp(pos - std::ios::streamoff(goby::LogEntry::crc_bytes_ + t.ByteSize() +
                                                         goby::LogEntry::type_bytes_ +
                                                         goby::LogEntry::group_bytes_ +
                                                         goby::LogEntry::scheme_bytes_ +
                                                         goby::LogEntry::size_bytes_ - 1));
            out_log_file.put(0xFF);
            out_log_file.seekp(pos);
        }

        case 6:
        {
            // corrupt the size field to make it just larger than the message
            auto pos = out_log_file.tellp();

            out_log_file.seekp(pos - std::ios::streamoff(goby::LogEntry::crc_bytes_ + t.ByteSize() +
                                                         goby::LogEntry::type_bytes_ +
                                                         goby::LogEntry::group_bytes_ +
                                                         goby::LogEntry::scheme_bytes_ + 1));
            out_log_file.put(0x14);
            out_log_file.seekp(pos);
        }
    }

    std::vector<CTDSample> ctds;
    for (int i = 0; i < nctd; ++i)
    {
        CTDSample ctd;
        ctd.set_temperature(i + 5);

        std::string encoded;
        codec.encode(&encoded, ctd);
        std::vector<unsigned char> data(encoded.begin(), encoded.end());
        goby::LogEntry entry(data, goby::MarshallingScheme::DCCL,
                             CTDSample::descriptor()->full_name(), ctdgroup);
        entry.serialize(&out_log_file);
        ctds.push_back(ctd);
    }
}

void insert_protobuf_file_desc(const google::protobuf::FileDescriptor* file_desc,
                               std::ofstream& out_log_file)
{
    for (int i = 0, n = file_desc->dependency_count(); i < n; ++i)
        insert_protobuf_file_desc(file_desc->dependency(i), out_log_file);

    if (written_file_desc_.count(file_desc) == 0)
    {
        std::cout << "Inserting file descriptor proto for: " << file_desc->name() << " : "
                  << file_desc << std::endl;
        written_file_desc_.insert(file_desc);

        google::protobuf::FileDescriptorProto file_desc_proto;
        file_desc->CopyTo(&file_desc_proto);
        std::vector<unsigned char> data(file_desc_proto.ByteSize());
        file_desc_proto.SerializeToArray(&data[0], data.size());
        goby::LogEntry entry(data, goby::MarshallingScheme::PROTOBUF,
                             google::protobuf::FileDescriptorProto::descriptor()->full_name(),
                             file_desc_group);
        entry.serialize(&out_log_file);
    }
    else
    {
        std::cout << "Skipping already written file descriptor proto for: " << file_desc->name()
                  << std::endl;
    }
}

void add_new_protobuf_type(int scheme, const std::string& protobuf_type,
                           std::ofstream& out_log_file)
{
    std::cout << "Hook called for scheme: " << scheme << " and type " << protobuf_type << std::endl;

    auto desc = dccl::DynamicProtobufManager::find_descriptor(protobuf_type);
    if (!desc)
    {
        goby::glog.is_warn() &&
            goby::glog << "Unknown protobuf type: " << protobuf_type << std::endl;
    }
    else
    {
        insert_protobuf_file_desc(desc->file(), out_log_file);
    }
}

int main(int argc, char* argv[])
{
    goby::glog.add_stream(goby::common::logger::DEBUG3, &std::cerr);
    goby::glog.set_name(argv[0]);

    codec.load<CTDSample>();

    int ntests = 7;

    for (int test = 0; test < ntests; ++test)
    {
        std::cout << "Running test " << test << std::endl;
        write_log(test);
        read_log(test);
    }

    std::cout << "all tests passed" << std::endl;
}
