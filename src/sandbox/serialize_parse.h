#ifndef SerializeParse20160607H
#define SerializeParse20160607H

namespace goby 
{
    enum class MarshallingScheme
    {
        CSTR,
        PROTOBUF,
        CAPTN_PROTO,
        MSGPACK
    };

    
    template<typename DataType, MarshallingScheme scheme>
        struct SP
        {
            static std::vector<char> serialize(const DataType& msg);
            static DataType parse(const std::vector<char>& bytes);
        };
    
    template<typename DataType>
        struct SP<DataType, MarshallingScheme::PROTOBUF>
    {
        static std::vector<char> serialize(const DataType& msg)
        {
            std::vector<char> bytes(msg.ByteSize(), 0);
            msg.SerializeToArray(bytes.data(), bytes.size());
            return bytes;
        }
        static DataType parse(const std::vector<char>& bytes)
        {
            DataType msg;
            msg.ParseFromArray(bytes.data(), bytes.size());
        }
    };
    
}

#endif
