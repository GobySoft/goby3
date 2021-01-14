// Copyright 2009-2020:
//   GobySoft, LLC (2013-)
//   Massachusetts Institute of Technology (2007-2014)
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

#ifndef GOBY_ACOMMS_QUEUE_QUEUE_H
#define GOBY_ACOMMS_QUEUE_QUEUE_H

#include <iostream> // for ostream
#include <list>     // for list
#include <map>      // for multimap
#include <memory>   // for shared_ptr
#include <stddef.h> // for size_t
#include <string>   // for string
#include <vector>   // for vector

#include <boost/any.hpp>                                    // for any
#include <boost/date_time/posix_time/posix_time_config.hpp> // for time_dur...
#include <boost/date_time/posix_time/ptime.hpp>             // for ptime
#include <boost/units/quantity.hpp>                         // for quantity
#include <google/protobuf/descriptor.h>                     // for Descriptor

#include "goby/acomms/dccl/dccl.h"         // for DCCLCodec
#include "goby/acomms/protobuf/queue.pb.h" // for QueuedMe...
#include "goby/time/convert.h"             // for convert

namespace google
{
namespace protobuf
{
class Message;
} // namespace protobuf
} // namespace google

namespace goby
{
namespace acomms
{
class QueueManager;
namespace protobuf
{
class ModemTransmission;
} // namespace protobuf

struct QueuedMessage
{
    std::shared_ptr<google::protobuf::Message> dccl_msg;
    protobuf::QueuedMessageMeta meta;
};

typedef std::list<QueuedMessage>::iterator messages_it;
using waiting_for_ack_it = std::multimap<unsigned int, messages_it>::iterator;

class Queue
{
  public:
    Queue(const google::protobuf::Descriptor* desc, QueueManager* parent,
          protobuf::QueuedMessageEntry cfg = protobuf::QueuedMessageEntry());

    bool push_message(const std::shared_ptr<google::protobuf::Message>& dccl_msg);
    bool push_message(const std::shared_ptr<google::protobuf::Message>& dccl_msg,
                      protobuf::QueuedMessageMeta meta);

    protobuf::QueuedMessageMeta meta_from_msg(const google::protobuf::Message& dccl_msg);

    boost::any find_queue_field(const std::string& field_name,
                                const google::protobuf::Message& msg);

    goby::acomms::QueuedMessage give_data(unsigned frame);
    bool pop_message(unsigned frame);
    bool pop_message_ack(unsigned frame, std::shared_ptr<google::protobuf::Message>& removed_msg);
    void stream_for_pop(const QueuedMessage& queued_msg);

    std::vector<std::shared_ptr<google::protobuf::Message> > expire();

    bool get_priority_values(double* priority, boost::posix_time::ptime* last_send_time,
                             const protobuf::ModemTransmission& request_msg,
                             const std::string& data);

    // returns true if empty
    bool clear_ack_queue(unsigned start_frame);

    void flush();

    size_t size() const { return messages_.size(); }

    boost::posix_time::ptime last_send_time() const { return last_send_time_; }

    boost::posix_time::ptime newest_msg_time() const
    {
        return size() ? goby::time::convert<boost::posix_time::ptime>(
                            messages_.back().meta.time_with_units())
                      : boost::posix_time::ptime();
    }

    void info(std::ostream* os) const;

    std::string name() const { return desc_->full_name(); }

    void set_cfg(const protobuf::QueuedMessageEntry& cfg)
    {
        cfg_ = cfg;
        process_cfg();
    }
    void process_cfg();

    const protobuf::QueuedMessageEntry& queue_message_options() { return cfg_; }

    const google::protobuf::Descriptor* descriptor() const { return desc_; }

    int id() { return goby::acomms::DCCLCodec::get()->id(desc_); }

  private:
    waiting_for_ack_it find_ack_value(messages_it it_to_find);
    messages_it next_message_it();

    void set_latest_metadata(const google::protobuf::FieldDescriptor* field,
                             const boost::any& field_value, const boost::any& wire_value);

    double time_duration2double(const boost::posix_time::time_duration& time_of_day);

  private:
    Queue& operator=(const Queue&);
    Queue(const Queue&);

    const google::protobuf::Descriptor* desc_;
    QueueManager* parent_;
    protobuf::QueuedMessageEntry cfg_;

    // maps role onto FieldDescriptor::full_name() or empty string if static role
    std::map<protobuf::QueuedMessageEntry::RoleType, std::string> roles_;

    boost::posix_time::ptime last_send_time_;

    std::list<QueuedMessage> messages_;

    // map frame number onto messages list iterator
    // can have multiples in the same frame now
    std::multimap<unsigned, messages_it> waiting_for_ack_;

    protobuf::QueuedMessageMeta static_meta_;
};
std::ostream& operator<<(std::ostream& os, const Queue& oq);
} // namespace acomms

} // namespace goby
#endif
