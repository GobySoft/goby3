#ifndef SerializeParse20160607H
#define SerializeParse20160607H

// TODO remove
#include <iostream>

#include <map>
#include <type_traits>

#include <google/protobuf/message.h>


namespace goby 
{

    struct MarshallingScheme
    {
        enum MarshallingSchemeEnum
        {
            CSTR = 0,
            PROTOBUF = 1,
            DCCL = 2,
            CAPTN_PROTO = 3,
            MSGPACK = 4
        };

        static std::string as_string(int e)
            {
                auto it = e2s.find(e);
                return it != e2s.end() ? it->second : std::to_string(e);
            }

    private:
        static std::map<int, std::string> e2s;
        static std::string unknown;
    };

    template<typename DataType, int scheme>
        struct SerializerParserHelper 
    { };

    
    template<typename DataType>
        struct SerializerParserHelper<DataType, MarshallingScheme::CSTR>
    {
        static std::vector<char> serialize(const DataType& msg)
        {
            std::vector<char> bytes(msg.begin(), msg.end());
            bytes.push_back('\0');
            return bytes;
        }

        static std::string type_name(const DataType& msg)
        { return "CSTR"; }
        
        static DataType parse(const std::vector<char>& bytes)
        {
            if(bytes.size())
                DataType msg(bytes.begin(), bytes.end()-1);
        }
    };
    
    template<typename DataType>
        struct SerializerParserHelper<DataType, MarshallingScheme::PROTOBUF>
    {
        static std::vector<char> serialize(const DataType& msg)
        {
            std::vector<char> bytes(msg.ByteSize(), 0);
            msg.SerializeToArray(bytes.data(), bytes.size());
            return bytes;
        }

        static std::string type_name(const DataType& msg)
        { return DataType::descriptor()->full_name(); }

        static DataType parse(const std::vector<char>& bytes)
        {
            DataType msg;
            msg.ParseFromArray(bytes.data(), bytes.size());
        }
    };

    // TODO, actually use DCCL Codec
    template<typename DataType>
        struct SerializerParserHelper<DataType, MarshallingScheme::DCCL>
    {
        static std::vector<char> serialize(const DataType& msg)
        {
            std::vector<char> bytes(msg.ByteSize(), 0);
            msg.SerializeToArray(bytes.data(), bytes.size());
            return bytes;
        }

        static std::string type_name(const DataType& msg)
        { return DataType::descriptor()->full_name(); }

        static DataType parse(const std::vector<char>& bytes)
        {
            DataType msg;
            msg.ParseFromArray(bytes.data(), bytes.size());
        }
    };

    template<typename T,
        typename std::enable_if<std::is_same<T, std::string>::value>::type* = nullptr>
        constexpr int scheme()
    {
        return goby::MarshallingScheme::CSTR;
    }

    
    namespace protobuf 
    {
        namespace detail
        {
            // used to select between DCCL messages (with added DCCLParameters Enumeration)
            // and normal Protobuf messages
            // in the DCCL case, both "scheme_protobuf_or_dccl" functions are valid, but the one
            // with "dccl_selector" as the argument is chosen because this doesn't require
            // upcasting to "protobuf_selector"
            // in the plain Protobuf case, just the "scheme_protobuf_or_dccl(protobuf_selector)" function
            // is chosen by template resolution, so this one is used.
            struct protobuf_selector {};
            struct dccl_selector : protobuf_selector {};
    
            template<typename T,
                typename std::enable_if<std::is_enum<typename T::DCCLParameters>::value>::type* = nullptr>
                constexpr int scheme_protobuf_or_dccl(dccl_selector)
                {
                    return goby::MarshallingScheme::DCCL;
                }    
            
            template<typename T>
                constexpr int scheme_protobuf_or_dccl(protobuf_selector)
            {
                return goby::MarshallingScheme::PROTOBUF;
            }
        }
    }
    
    template<typename T,
        typename std::enable_if<std::is_base_of<google::protobuf::Message, T>::value>::type* = nullptr>
        constexpr int scheme()
    {
        return protobuf::detail::scheme_protobuf_or_dccl<T>(protobuf::detail::dccl_selector());
    }

    
}

#endif
