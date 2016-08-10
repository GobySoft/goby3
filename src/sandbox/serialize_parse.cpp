#include "serialize_parse.h"

std::map<int, std::string> goby::MarshallingScheme::e2s =
{ {CSTR, "CSTR"},
  {PROTOBUF, "PROTOBUF"},
  {DCCL, "DCCL"},
  {CAPTN_PROTO, "CAPTN_PROTO"},
  {MSGPACK, "MSGPACK"} };

std::unique_ptr<dccl::Codec> goby::DCCLSerializerParserHelperBase::codec_(nullptr);
std::unordered_map<std::type_index, std::unique_ptr<goby::DCCLSerializerParserHelperBase::LoaderBase>> goby::DCCLSerializerParserHelperBase::loader_map_;
