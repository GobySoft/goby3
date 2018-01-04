#include "goby/common/logger.h"
#include "goby/middleware/log.h"
#include "goby/middleware/serialize_parse.h"

#include "test.pb.h"

const goby::Group tempgroup("groups::temp");
const goby::Group ctdgroup("groups::ctd");
int nctd = 5;

void read_log(int test)
{
    goby::LogEntry::reset();        
    
    std::ifstream in_log_file("/tmp/goby3_test_log.goby");
    try
    {
        goby::LogEntry entry;
        entry.parse(&in_log_file);
        assert(entry.scheme() == goby::MarshallingScheme::PROTOBUF);
        assert(entry.group() == tempgroup);
        assert(entry.type() == TempSample::descriptor()->full_name());
        assert(test != 3 && test != 4);
    }
    catch(goby::Exception& e)
    {
        std::cerr << e.what() << std::endl;
        assert(test == 3 || test == 4);
    }
    
    if(test == 4)
    {
        goby::LogEntry entry;
        entry.parse(&in_log_file);
        assert(entry.scheme() == goby::MarshallingScheme::PROTOBUF);
        // corrupted index
        assert(entry.group() == "_unknown1_");
        assert(entry.type() == TempSample::descriptor()->full_name());
    }
    
    
    for(int i = 0; i < nctd; ++i)
    {
        goby::LogEntry entry;
        entry.parse(&in_log_file);
        assert(entry.scheme() == goby::MarshallingScheme::DCCL);
        assert(entry.group() == ctdgroup);
        assert(entry.type() == CTDSample::descriptor()->full_name());
    }

    // eof
    try
    {
        goby::LogEntry entry;
        entry.parse(&in_log_file);
        bool expected_eof = false;
        assert(expected_eof);
    }
    catch (std::ifstream::failure e) {
        assert(in_log_file.eof());
    }
}

void write_log(int test)
{
    goby::LogEntry::reset();        
    std::ofstream out_log_file("/tmp/goby3_test_log.goby");

    switch(test)
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
        goby::LogEntry entry(data, goby::MarshallingScheme::PROTOBUF, TempSample::descriptor()->full_name(), tempgroup);
        entry.serialize(&out_log_file);
    }

    switch(test)
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
            out_log_file.seekp(pos-std::ios::streamoff(6));
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
        
            
    }

    
    std::vector<CTDSample> ctds;
    for(int i = 0; i < nctd; ++i)
    {
        CTDSample ctd;
        ctd.set_temperature(i*100);
        std::vector<unsigned char> data(ctd.ByteSize());
        ctd.SerializeToArray(&data[0], data.size());
        goby::LogEntry entry(data, goby::MarshallingScheme::DCCL, CTDSample::descriptor()->full_name(), ctdgroup);
        entry.serialize(&out_log_file);
        ctds.push_back(ctd);
    }
}

int main(int argc, char* argv[])
{
    goby::glog.add_stream(goby::common::logger::DEBUG3, &std::cerr);
    goby::glog.set_name(argv[0]);

    int ntests = 5;
    
    for(int test = 0; test < ntests; ++test)
    {
        std::cout << "Running test " << test << std::endl;
        write_log(test);
        read_log(test);
    }    
   
    
    
    std::cout << "all tests passed" << std::endl;
}

