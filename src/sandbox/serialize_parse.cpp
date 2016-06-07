#include "serialize_parse.h"

std::map<int, std::string> goby::MarshallingScheme::e2s = { {CSTR, "CSTR"},
                                                            {PROTOBUF, "PROTOBUF"},
                                                            {DCCL, "DCCL"},
                                                            {CAPTN_PROTO, "CAPTN_PROTO"},
                                                            {MSGPACK, "MSGPACK"} };
