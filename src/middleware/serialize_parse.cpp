#include "goby/middleware/protobuf/interprocess_data.pb.h"
#include "goby/common/logger.h"

#include "serialize_parse.h"


std::map<int, std::string> goby::MarshallingScheme::e2s =
{ {CSTR, "CSTR"},
  {PROTOBUF, "PROTOBUF"},
  {DCCL, "DCCL"},
  {CAPTN_PROTO, "CAPTN_PROTO"},
  {MSGPACK, "MSGPACK"} };

std::unique_ptr<dccl::Codec> goby::DCCLSerializerParserHelperBase::codec_(nullptr);
std::unordered_map<std::type_index, std::unique_ptr<goby::DCCLSerializerParserHelperBase::LoaderBase>> goby::DCCLSerializerParserHelperBase::loader_map_;
std::mutex goby::DCCLSerializerParserHelperBase::dccl_mutex_;

void goby::DCCLSerializerParserHelperBase::load_forwarded_subscription(const goby::protobuf::DCCLSubscription& sub)
{
    std::lock_guard<std::mutex> lock(dccl_mutex_);

    // check that we don't already have this type available
    if(auto* desc = dccl::DynamicProtobufManager::find_descriptor(sub.protobuf_name()))
    {
        codec().load(desc);
    }
    else
    {
        for(const auto& file_desc : sub.file_descriptor())
            dccl::DynamicProtobufManager::add_protobuf_file(file_desc);
        if(auto* desc = dccl::DynamicProtobufManager::find_descriptor(sub.protobuf_name()))
            codec().load(desc);
        else
            goby::glog.is(goby::common::logger::DEBUG3) && goby::glog << "Failed to load DCCL message sent via forwarded subscription: " << sub.protobuf_name() << std::endl;
    }
}
