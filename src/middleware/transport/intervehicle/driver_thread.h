// Copyright 2019-2020:
//   GobySoft, LLC (2013-)
//   Community contributors (see AUTHORS file)
// File authors:
//   Toby Schneider <toby@gobysoft.org>
//
//
// This file is part of the Goby Underwater Autonomy Project Libraries
// ("The Goby Libraries").
//
// The Goby Libraries are free software: you can redistribute them and/or modify
// them under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 2.1 of the License, or
// (at your option) any later version.
//
// The Goby Libraries are distributed in the hope that they will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with Goby.  If not, see <http://www.gnu.org/licenses/>.

#ifndef DriverThread20190619H
#define DriverThread20190619H

#include "goby/acomms/amac.h"
#include "goby/acomms/buffer/dynamic_buffer.h"

#include "goby/middleware/marshalling/dccl.h"

#include "goby/middleware/application/thread.h"
#include "goby/middleware/group.h"
#include "goby/middleware/protobuf/intervehicle.pb.h"
#include "goby/middleware/transport/interprocess.h"
#include "goby/middleware/transport/interthread.h"

namespace goby
{
namespace acomms
{
class ModemDriverBase;

} // namespace acomms

namespace middleware
{
namespace protobuf
{
inline size_t data_size(const SerializerTransporterMessage& msg) { return msg.data().size(); }

inline bool operator==(const SerializerTransporterMessage& a, const SerializerTransporterMessage& b)
{
    return (a.key().serialize_time() == b.key().serialize_time() &&
            a.key().marshalling_scheme() == b.key().marshalling_scheme() &&
            a.key().type() == b.key().type() && a.key().group() == b.key().group() &&
            a.data() == b.data());
}

inline bool operator<(const SerializerTransporterMessage& a, const SerializerTransporterMessage& b)
{
    if (a.key().serialize_time() != b.key().serialize_time())
        return a.key().serialize_time() < b.key().serialize_time();
    else if (a.key().marshalling_scheme() != b.key().marshalling_scheme())
        return a.key().marshalling_scheme() < b.key().marshalling_scheme();
    else if (a.key().type() != b.key().type())
        return a.key().type() < b.key().type();
    else if (a.key().group() != b.key().group())
        return a.key().group() < b.key().group();
    else
        return a.data() < b.data();
}

} // namespace protobuf

namespace intervehicle
{
template <typename Data>
std::shared_ptr<goby::middleware::protobuf::SerializerTransporterMessage>
serialize_publication(const Data& d, const Group& group, const Publisher<Data>& publisher)
{
    std::vector<char> bytes(SerializerParserHelper<Data, MarshallingScheme::DCCL>::serialize(d));
    std::string* sbytes = new std::string(bytes.begin(), bytes.end());
    auto msg = std::make_shared<goby::middleware::protobuf::SerializerTransporterMessage>();

    auto* key = msg->mutable_key();
    key->set_marshalling_scheme(MarshallingScheme::DCCL);
    key->set_type(SerializerParserHelper<Data, MarshallingScheme::DCCL>::type_name(d));
    key->set_group(std::string(group));
    key->set_group_numeric(group.numeric());
    auto now = goby::time::SystemClock::now<goby::time::MicroTime>();
    key->set_serialize_time_with_units(now);
    *key->mutable_cfg() = publisher.cfg();
    msg->set_allocated_data(sbytes);
    return msg;
}

namespace groups
{
constexpr Group subscription_forward{"goby::middleware::intervehicle::subscription_forward",
                                     Group::broadcast_group};

constexpr Group modem_data_out{"goby::middleware::intervehicle::modem_data_out"};
constexpr Group modem_data_in{"goby::middleware::intervehicle::modem_data_in"};
constexpr Group modem_ack_in{"goby::middleware::intervehicle::modem_ack_in"};
constexpr Group modem_expire_in{"goby::middleware::intervehicle::modem_expire_in"};

constexpr Group modem_subscription_forward_tx{
    "goby::middleware::intervehicle::modem_subscription_forward_tx"};
constexpr Group modem_subscription_forward_rx{
    "goby::middleware::intervehicle::modem_subscription_forward_rx"};
constexpr Group modem_driver_ready{"goby::middleware::intervehicle::modem_driver_ready"};

constexpr Group metadata_request{"goby::middleware::intervehicle::metadata_request"};

} // namespace groups

class ModemDriverThread
    : public goby::middleware::Thread<intervehicle::protobuf::PortalConfig::LinkConfig,
                                      InterProcessForwarder<InterThreadTransporter>>
{
  public:
    using buffer_data_type = goby::middleware::protobuf::SerializerTransporterMessage;
    using modem_id_type = goby::acomms::DynamicBuffer<buffer_data_type>::modem_id_type;
    using subbuffer_id_type = goby::acomms::DynamicBuffer<buffer_data_type>::subbuffer_id_type;

    ModemDriverThread(const intervehicle::protobuf::PortalConfig::LinkConfig& cfg);
    void loop() override;
    int tx_queue_size() { return buffer_.size(); }

  private:
    void _data_request(goby::acomms::protobuf::ModemTransmission* msg);
    void _buffer_message(
        std::shared_ptr<const goby::middleware::protobuf::SerializerTransporterMessage> msg);
    void _receive(const goby::acomms::protobuf::ModemTransmission& rx_msg);
    void _forward_subscription(intervehicle::protobuf::Subscription subscription);
    void _accept_subscription(const intervehicle::protobuf::Subscription& subscription);
    void _expire_value(const goby::time::SteadyClock::time_point now,
                       const goby::acomms::DynamicBuffer<buffer_data_type>::Value& value,
                       intervehicle::protobuf::ExpireData::ExpireReason reason);

    subbuffer_id_type _create_buffer_id(unsigned dccl_id, unsigned group);

    subbuffer_id_type
    _create_buffer_id(const goby::middleware::protobuf::SerializerTransporterKey& key)
    {
        return _create_buffer_id(detail::DCCLSerializerParserHelperBase::id(key.type()),
                                 key.group_numeric());
    }

    subbuffer_id_type _create_buffer_id(const intervehicle::protobuf::Subscription& subscription)
    {
        return _create_buffer_id(subscription.dccl_id(), subscription.group());
    }

    void _create_buffer(modem_id_type dest_id, subbuffer_id_type buffer_id,
                        const std::vector<goby::acomms::protobuf::DynamicBufferConfig>& cfgs);

    bool _dest_is_in_subnet(modem_id_type dest_id)
    {
        bool dest_in_subnet =
            (dest_id & cfg().subnet_mask()) == (cfg().modem_id() & cfg().subnet_mask());
        if (!dest_in_subnet)
            goby::glog.is_debug3() && goby::glog
                                          << "Dest: " << dest_id
                                          << " is not in subnet (our id: " << cfg().modem_id()
                                          << ", mask: " << cfg().subnet_mask();

        return dest_in_subnet;
    }

  private:
    std::unique_ptr<InterThreadTransporter> interthread_;
    std::unique_ptr<InterProcessForwarder<InterThreadTransporter>> interprocess_;

    std::map<subbuffer_id_type, goby::middleware::protobuf::SerializerTransporterKey>
        publisher_buffer_cfg_;

    std::map<modem_id_type, std::map<subbuffer_id_type, intervehicle::protobuf::Subscription>>
        subscriber_buffer_cfg_;

    std::map<subbuffer_id_type, std::set<modem_id_type>> subbuffers_created_;

    goby::middleware::protobuf::SerializerTransporterKey subscription_key_;
    std::set<modem_id_type> subscription_subbuffers_;

    goby::acomms::DynamicBuffer<buffer_data_type> buffer_;

    using frame_type = int;
    std::map<frame_type, std::vector<goby::acomms::DynamicBuffer<buffer_data_type>::Value>>
        pending_ack_;

    std::unique_ptr<goby::acomms::ModemDriverBase> driver_;
    goby::acomms::MACManager mac_;
};

} // namespace intervehicle
} // namespace middleware
} // namespace goby

#endif
