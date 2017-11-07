#include "goby/middleware/serialize_parse.h"
#include <vector>

namespace goby
{
    struct MyMarshallingScheme
    {
        enum MyMarshallingSchemeEnum
        {
            DEQUECHAR = 1000
        };
    };
    
    template<typename DataType>
        struct SerializerParserHelper<DataType, MyMarshallingScheme::DEQUECHAR>
    {
        static std::vector<char> serialize(const DataType& msg)
        {
            std::vector<char> bytes(msg.begin(), msg.end());
            return bytes;
        }
        
        static std::string type_name()
        { return "DEQUECHAR"; }

        static DataType parse(const std::vector<char>& bytes)
        {
            if(bytes.size())
                DataType msg(bytes.begin(), bytes.end()-1);
        }
    };


    template<typename T>
        constexpr int scheme(typename std::enable_if<std::is_same<T, std::deque<char>>::value>::type* = 0)
    {
        return MyMarshallingScheme::DEQUECHAR;
    }
}
