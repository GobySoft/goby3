// Copyright 2009-2018 Toby Schneider (http://gobysoft.org/index.wt/people/toby)
//                     GobySoft, LLC (2013-)
//                     Massachusetts Institute of Technology (2007-2014)
//                     Community contributors (see AUTHORS file)
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

#include "dccl/dynamic_protobuf_manager.h"
#include "goby/acomms/acomms_constants.h"
#include "goby/acomms/dccl.h"
#include "goby/common/logger.h"

#include "queue.h"
#include "queue_manager.h"

using goby::common::goby_time;

using namespace goby::common::logger;
using goby::util::as;

goby::acomms::Queue::Queue(const google::protobuf::Descriptor* desc, QueueManager* parent,
                           const protobuf::QueuedMessageEntry& cfg)
    : desc_(desc),
      parent_(parent),
      cfg_(cfg),
      last_send_time_(time::SystemClock::now<boost::posix_time::ptime>())
{
    process_cfg();
}

// add a new message
bool goby::acomms::Queue::push_message(std::shared_ptr<google::protobuf::Message> dccl_msg)
{
    protobuf::QueuedMessageMeta meta = meta_from_msg(*dccl_msg);
    return push_message(dccl_msg, meta);
}

bool goby::acomms::Queue::push_message(std::shared_ptr<google::protobuf::Message> dccl_msg,
                                       protobuf::QueuedMessageMeta meta)
{
    // loopback if set
    if (parent_->manip_manager_.has(id(), protobuf::LOOPBACK) && !meta.has_encoded_message())
    {
        glog.is(DEBUG1) && glog << group(parent_->glog_push_group())
                                << parent_->msg_string(dccl_msg->GetDescriptor())
                                << ": LOOPBACK manipulator set, sending back to decoder"
                                << std::endl;
        parent_->signal_receive(*dccl_msg);
    }

    parent_->signal_out_route(&meta, *dccl_msg, parent_->cfg_.modem_id());

    glog.is(DEBUG1) &&
        glog << group(parent_->glog_push_group()) << parent_->msg_string(dccl_msg->GetDescriptor())
             << ": attempting to push message (destination: " << meta.dest() << ")" << std::endl;

    // no queue manipulator set
    if (parent_->manip_manager_.has(id(), protobuf::NO_QUEUE))
    {
        glog.is(DEBUG1) && glog << group(parent_->glog_push_group())
                                << parent_->msg_string(dccl_msg->GetDescriptor())
                                << ": not queuing: NO_QUEUE manipulator is set" << std::endl;
        return true;
    }
    // message is to us, auto-loopback
    else if (meta.dest() == parent_->modem_id_)
    {
        glog.is(DEBUG1) && glog << group(parent_->glog_push_group())
                                << "Message is for us: using loopback, not physical interface"
                                << std::endl;

        parent_->signal_receive(*dccl_msg);

        // provide an ACK if desired
        if ((meta.has_ack_requested() && meta.ack_requested()) || queue_message_options().ack())
        {
            protobuf::ModemTransmission ack_msg;
            ack_msg.set_time_with_units(goby::time::SystemClock::now<time::MicroTime>());
            ack_msg.set_src(meta.dest());
            ack_msg.set_dest(meta.dest());
            ack_msg.set_type(protobuf::ModemTransmission::ACK);

            parent_->signal_ack(ack_msg, *dccl_msg);
        }
        return true;
    }

    if (!meta.has_time())
        meta.set_time_with_units(goby::time::SystemClock::now<time::MicroTime>());

    if (meta.non_repeated_size() == 0)
    {
        goby::glog.is(DEBUG1) && glog << group(parent_->glog_out_group()) << warn
                                      << "empty message attempted to be pushed to queue " << name()
                                      << std::endl;
        return false;
    }

    if (!meta.has_ack_requested())
        meta.set_ack_requested(queue_message_options().ack());
    messages_.push_back(QueuedMessage());
    messages_.back().meta = meta;
    messages_.back().dccl_msg = dccl_msg;

    glog.is(DEBUG1) && glog << group(parent_->glog_push_group())
                            << "pushed to send stack (queue size " << size() << "/"
                            << queue_message_options().max_queue() << ")" << std::endl;

    glog.is(DEBUG2) && glog << group(parent_->glog_push_group()) << "Message: " << *dccl_msg
                            << std::endl;
    glog.is(DEBUG2) && glog << group(parent_->glog_push_group()) << "Meta: " << meta << std::endl;

    // pop messages off the stack if the queue is full
    if (queue_message_options().max_queue() &&
        messages_.size() > queue_message_options().max_queue())
    {
        messages_it it_to_erase =
            queue_message_options().newest_first() ? messages_.begin() : messages_.end();

        // want "back" iterator not "end"
        if (it_to_erase == messages_.end())
            --it_to_erase;

        // if we were waiting for an ack for this, erase that too
        waiting_for_ack_it it = find_ack_value(it_to_erase);
        if (it != waiting_for_ack_.end())
            waiting_for_ack_.erase(it);

        glog.is(DEBUG1) && glog << group(parent_->glog_pop_group()) << "queue exceeded for "
                                << name() << ". removing: " << it_to_erase->meta << std::endl;

        messages_.erase(it_to_erase);
    }

    return true;
}

goby::acomms::protobuf::QueuedMessageMeta
goby::acomms::Queue::meta_from_msg(const google::protobuf::Message& dccl_msg)
{
    protobuf::QueuedMessageMeta meta = static_meta_;
    meta.set_non_repeated_size(parent_->codec_->size(dccl_msg));

    if (!roles_[protobuf::QueuedMessageEntry::DESTINATION_ID].empty())
    {
        boost::any field_value =
            find_queue_field(roles_[protobuf::QueuedMessageEntry::DESTINATION_ID], dccl_msg);

        int dest = BROADCAST_ID;
        if (field_value.type() == typeid(std::int32_t))
            dest = boost::any_cast<std::int32_t>(field_value);
        else if (field_value.type() == typeid(std::int64_t))
            dest = boost::any_cast<std::int64_t>(field_value);
        else if (field_value.type() == typeid(std::uint32_t))
            dest = boost::any_cast<std::uint32_t>(field_value);
        else if (field_value.type() == typeid(std::uint64_t))
            dest = boost::any_cast<std::uint64_t>(field_value);
        else if (!field_value.empty())
            throw(QueueException("Invalid type " + std::string(field_value.type().name()) +
                                 " given for (queue_field).is_dest. Expected integer type"));

        goby::glog.is(DEBUG2) && goby::glog << group(parent_->glog_push_group_)
                                            << "setting dest to " << dest << std::endl;

        meta.set_dest(dest);
    }

    if (!roles_[protobuf::QueuedMessageEntry::SOURCE_ID].empty())
    {
        boost::any field_value =
            find_queue_field(roles_[protobuf::QueuedMessageEntry::SOURCE_ID], dccl_msg);

        int src = BROADCAST_ID;
        if (field_value.type() == typeid(std::int32_t))
            src = boost::any_cast<std::int32_t>(field_value);
        else if (field_value.type() == typeid(std::int64_t))
            src = boost::any_cast<std::int64_t>(field_value);
        else if (field_value.type() == typeid(std::uint32_t))
            src = boost::any_cast<std::uint32_t>(field_value);
        else if (field_value.type() == typeid(std::uint64_t))
            src = boost::any_cast<std::uint64_t>(field_value);
        else if (!field_value.empty())
            throw(QueueException("Invalid type " + std::string(field_value.type().name()) +
                                 " given for (queue_field).is_src. Expected integer type"));

        goby::glog.is(DEBUG2) && goby::glog << group(parent_->glog_push_group_)
                                            << "setting source to " << src << std::endl;

        meta.set_src(src);
    }

    if (!roles_[protobuf::QueuedMessageEntry::TIMESTAMP].empty())
    {
        boost::any field_value =
            find_queue_field(roles_[protobuf::QueuedMessageEntry::TIMESTAMP], dccl_msg);

        if (field_value.type() == typeid(std::uint64_t))
            meta.set_time(boost::any_cast<std::uint64_t>(field_value));
        else if (field_value.type() == typeid(double))
            meta.set_time(static_cast<std::uint64_t>(boost::any_cast<double>(field_value)) * 1e6);
        else if (field_value.type() == typeid(std::string))
            meta.set_time_with_units(
                time::convert<time::MicroTime>(goby::util::as<boost::posix_time::ptime>(
                    boost::any_cast<std::string>(field_value))));
        else if (!field_value.empty())
            throw(QueueException(
                "Invalid type " + std::string(field_value.type().name()) +
                " given for (goby.field).queue.is_time. Expected std::uint64_t contained "
                "microseconds since UNIX, double containing seconds since UNIX or std::string "
                "containing as<std::string>(boost::posix_time::ptime)"));

        goby::glog.is(DEBUG2) &&
            goby::glog << group(parent_->glog_push_group_) << "setting time to "
                       << time::convert<boost::posix_time::ptime>(meta.time_with_units())
                       << std::endl;
    }

    glog.is(DEBUG2) && glog << group(parent_->glog_push_group()) << "Meta: " << meta << std::endl;
    return meta;
}

boost::any goby::acomms::Queue::find_queue_field(const std::string& field_name,
                                                 const google::protobuf::Message& msg)
{
    const google::protobuf::Message* current_msg = &msg;
    const google::protobuf::Descriptor* current_desc = current_msg->GetDescriptor();

    // split name on "." as subfield delimiter
    std::vector<std::string> field_names;
    boost::split(field_names, field_name, boost::is_any_of("."));

    for (int i = 0, n = field_names.size(); i < n; ++i)
    {
        const google::protobuf::FieldDescriptor* field_desc =
            current_desc->FindFieldByName(field_names[i]);
        if (!field_desc)
            throw(QueueException("No such field called " + field_name + " in msg " +
                                 current_desc->full_name()));

        if (field_desc->is_repeated())
            throw(QueueException("Cannot assign a Queue role to a repeated field"));

        auto helper = goby::acomms::DCCLTypeHelper::find(field_desc);

        // last field_name
        if (i == (n - 1))
        {
            return helper->get_value(field_desc, *current_msg);
        }
        else if (field_desc->type() != google::protobuf::FieldDescriptor::TYPE_MESSAGE)
        {
            throw(QueueException("Cannot access child fields of a non-message field: " +
                                 field_names[i]));
        }
        else
        {
            boost::any value = helper->get_value(field_desc, *current_msg);
            if (value.empty()) // no submessage in this message
                return boost::any();
            else
            {
                current_msg = boost::any_cast<const google::protobuf::Message*>(value);
                current_desc = current_msg->GetDescriptor();
            }
        }
    }

    return boost::any();
}

goby::acomms::messages_it goby::acomms::Queue::next_message_it()
{
    messages_it it_to_give =
        queue_message_options().newest_first() ? messages_.end() : messages_.begin();
    if (it_to_give == messages_.end())
        --it_to_give; // want "back" iterator not "end"

    // find a value that isn't already waiting to be acknowledged
    while (find_ack_value(it_to_give) != waiting_for_ack_.end())
        queue_message_options().newest_first() ? --it_to_give : ++it_to_give;

    return it_to_give;
}

goby::acomms::QueuedMessage goby::acomms::Queue::give_data(unsigned frame)
{
    messages_it it_to_give = next_message_it();

    bool ack = it_to_give->meta.ack_requested();
    // broadcast cannot acknowledge
    if (it_to_give->meta.dest() == BROADCAST_ID && ack == true)
    {
        glog.is(DEBUG1) && glog << group(parent_->glog_pop_group()) << parent_->msg_string(desc_)
                                << ": setting ack=false because BROADCAST (0) cannot ACK messages"
                                << std::endl;
        ack = false;
    }

    it_to_give->meta.set_ack_requested(ack);

    if (ack)
        waiting_for_ack_.insert(std::pair<unsigned, messages_it>(frame, it_to_give));

    last_send_time_ = time::SystemClock::now<boost::posix_time::ptime>();
    it_to_give->meta.set_last_sent_time_with_units(time::convert<time::MicroTime>(last_send_time_));

    return *it_to_give;
}

double goby::acomms::Queue::time_duration2double(boost::posix_time::time_duration time_of_day)
{
    using namespace boost::posix_time;

    // prevent overflows in getting total seconds with call to ptime::total_seconds
    if (time_of_day.hours() > (0x7FFFFFFF / 3600))
        return std::numeric_limits<double>::infinity();
    else
        return (double(time_of_day.total_seconds()) +
                double(time_of_day.fractional_seconds()) /
                    double(time_duration::ticks_per_second()));
}

// gives priority values. returns false if in blackout interval or if no data or if messages of wrong size, true if not in blackout
bool goby::acomms::Queue::get_priority_values(double* priority,
                                              boost::posix_time::ptime* last_send_time,
                                              const protobuf::ModemTransmission& request_msg,
                                              const std::string& data)
{
    *priority = time_duration2double(
                    (time::SystemClock::now<boost::posix_time::ptime>() - last_send_time_)) /
                queue_message_options().ttl() * queue_message_options().value_base();

    *last_send_time = last_send_time_;

    // no messages left to send
    if (messages_.size() <= waiting_for_ack_.size())
        return false;

    protobuf::QueuedMessageMeta& next_msg = next_message_it()->meta;

    // for followup user-frames, destination must be either zero (broadcast)
    // or the same as the first user-frame

    if (last_send_time_ + boost::posix_time::seconds(queue_message_options().blackout_time()) >
        time::SystemClock::now<boost::posix_time::ptime>())
    {
        glog.is(DEBUG1) && glog << group(parent_->glog_priority_group()) << "\t" << name()
                                << " is in blackout" << std::endl;
        return false;
    }
    // wrong size
    else if (request_msg.has_max_frame_bytes() &&
             (next_msg.non_repeated_size() > (request_msg.max_frame_bytes() - data.size())))
    {
        glog.is(DEBUG1) && glog << group(parent_->glog_priority_group()) << "\t" << name()
                                << " next message is too large {" << next_msg.non_repeated_size()
                                << "}" << std::endl;
        return false;
    }
    // wrong destination
    else if ((request_msg.has_dest() &&
              !(request_msg.dest() == QUERY_DESTINATION_ID  // can set to a real destination
                || next_msg.dest() == BROADCAST_ID          // can switch to a real destination
                || request_msg.dest() == next_msg.dest()))) // same as real destination
    {
        glog.is(DEBUG1) && glog << group(parent_->glog_priority_group()) << "\t" << name()
                                << " next message has wrong destination (must be BROADCAST (0) or "
                                   "same as first user-frame, is "
                                << next_msg.dest() << ")" << std::endl;
        return false;
    }
    // wrong ack value UNLESS message can be broadcast
    else if ((request_msg.has_ack_requested() && !request_msg.ack_requested() &&
              next_msg.ack_requested() && request_msg.dest() != acomms::BROADCAST_ID))
    {
        glog.is(DEBUG1) && glog << group(parent_->glog_priority_group()) << "\t" << name()
                                << " next message requires ACK and the packet does not"
                                << std::endl;
        return false;
    }
    else // ok!
    {
        glog.is(DEBUG1) && glog << group(parent_->glog_priority_group()) << "\t" << name() << " ("
                                << next_msg.non_repeated_size() << "B) has priority value"
                                << ": " << *priority << std::endl;
        return true;
    }
}

bool goby::acomms::Queue::pop_message(unsigned frame)
{
    std::list<QueuedMessage>::iterator back_it = messages_.end();
    --back_it; // gives us "back" iterator
    std::list<QueuedMessage>::iterator front_it = messages_.begin();

    // find the first message that isn't waiting for an ack
    std::list<QueuedMessage>::iterator it =
        queue_message_options().newest_first() ? back_it : front_it;

    while (true)
    {
        if (!it->meta.ack_requested())
        {
            stream_for_pop(*it);
            messages_.erase(it);
            return true;
        }

        if (it == (queue_message_options().newest_first() ? front_it : back_it))
            return false;

        queue_message_options().newest_first() ? --it : ++it;
    }
    return false;
}

bool goby::acomms::Queue::pop_message_ack(unsigned frame,
                                          std::shared_ptr<google::protobuf::Message>& removed_msg)
{
    // pop message from the ack stack
    if (waiting_for_ack_.count(frame))
    {
        // remove a messages in this frame that needs ack
        waiting_for_ack_it it = waiting_for_ack_.find(frame);
        removed_msg = (it->second)->dccl_msg;

        stream_for_pop(*it->second);

        // remove the message
        messages_.erase(it->second);
        // clear the acknowledgement map entry for this message
        waiting_for_ack_.erase(it);
    }
    else
    {
        return false;
    }

    return true;
}

void goby::acomms::Queue::stream_for_pop(const QueuedMessage& queued_msg)
{
    glog.is(DEBUG1) && glog << group(parent_->glog_pop_group()) << parent_->msg_string(desc_)
                            << ": popping from send stack"
                            << " (queue size " << size() - 1 << "/"
                            << queue_message_options().max_queue() << ")" << std::endl;

    glog.is(DEBUG2) && glog << group(parent_->glog_push_group())
                            << "Message: " << *queued_msg.dccl_msg << std::endl;
    glog.is(DEBUG2) && glog << group(parent_->glog_push_group()) << "Meta: " << queued_msg.meta
                            << std::endl;
}

std::vector<std::shared_ptr<google::protobuf::Message> > goby::acomms::Queue::expire()
{
    std::vector<std::shared_ptr<google::protobuf::Message> > expired_msgs;

    while (!messages_.empty())
    {
        if ((time::convert<boost::posix_time::ptime>(messages_.front().meta.time_with_units()) +
             boost::posix_time::seconds(queue_message_options().ttl())) <
            time::SystemClock::now<boost::posix_time::ptime>())
        {
            expired_msgs.push_back(messages_.front().dccl_msg);
            glog.is(DEBUG1) && glog << group(parent_->glog_pop_group()) << "expiring"
                                    << " from send stack " << name() << " "
                                    << messages_.front().meta.time() << " (qsize " << size() - 1
                                    << "/" << queue_message_options().max_queue()
                                    << "): " << *messages_.front().dccl_msg << std::endl;
            // if we were waiting for an ack for this, erase that too
            waiting_for_ack_it it = find_ack_value(messages_.begin());
            if (it != waiting_for_ack_.end())
                waiting_for_ack_.erase(it);

            messages_.pop_front();
        }
        else
        {
            return expired_msgs;
        }
    }

    return expired_msgs;
}

goby::acomms::waiting_for_ack_it goby::acomms::Queue::find_ack_value(messages_it it_to_find)
{
    waiting_for_ack_it n = waiting_for_ack_.end();
    for (waiting_for_ack_it it = waiting_for_ack_.begin(); it != n; ++it)
    {
        if (it->second == it_to_find)
            return it;
    }
    return n;
}

void goby::acomms::Queue::info(std::ostream* os) const
{
    *os << "== Begin Queue [[" << name() << "]] ==\n";
    *os << "Contains " << messages_.size() << " message(s)."
        << "\n"
        << "Configured options: \n"
        << cfg_.ShortDebugString();
    *os << "\n== End Queue [[" << name() << "]] ==\n";
}

void goby::acomms::Queue::flush()
{
    glog.is(DEBUG1) && glog << group(parent_->glog_pop_group()) << "flushing stack " << name()
                            << " (qsize 0)" << std::endl;
    messages_.clear();
    waiting_for_ack_.clear();
}

bool goby::acomms::Queue::clear_ack_queue(unsigned start_frame)
{
    for (waiting_for_ack_it it = waiting_for_ack_.begin(), end = waiting_for_ack_.end(); it != end;)
    {
        // clear out acks for frames whose ack wait time has expired (or whose frame
        // number has come around again. This should avoid losing unack'd data.
        if (it->first >= start_frame)
        {
            glog.is(DEBUG1) &&
                glog << group(parent_->glog_pop_group()) << name()
                     << ": Clearing ack for queue because last_frame >= current_frame" << std::endl;
            waiting_for_ack_.erase(it++);
        }
        else if (it->second->meta.last_sent_time_with_units() +
                     time::MicroTime(parent_->cfg_.minimum_ack_wait_seconds() *
                                     boost::units::si::seconds) <
                 time::SystemClock::now<time::MicroTime>())
        {
            glog.is(DEBUG1) && glog << group(parent_->glog_pop_group()) << name()
                                    << ": Clearing ack for queue because "
                                    << parent_->cfg_.minimum_ack_wait_seconds()
                                    << " seconds has elapsed since last send. Last send:"
                                    << it->second->meta.last_sent_time() << std::endl;
            waiting_for_ack_.erase(it++);
        }
        else
        {
            ++it;
        }
    }
    return waiting_for_ack_.empty();
}

std::ostream& goby::acomms::operator<<(std::ostream& os, const goby::acomms::Queue& oq)
{
    oq.info(&os);
    return os;
}

void goby::acomms::Queue::process_cfg()
{
    roles_.clear();
    static_meta_.Clear();

    // used to check that the FIELD_VALUE roles fields actually exist
    auto new_msg = dccl::DynamicProtobufManager::new_protobuf_message<
        std::shared_ptr<google::protobuf::Message> >(desc_);

    for (int i = 0, n = cfg_.role_size(); i < n; ++i)
    {
        std::string role_field;

        switch (cfg_.role(i).setting())
        {
            case protobuf::QueuedMessageEntry::Role::STATIC:
            {
                if (!cfg_.role(i).has_static_value())
                    throw(QueueException(
                        "Role " + protobuf::QueuedMessageEntry::RoleType_Name(cfg_.role(i).type()) +
                        " is set to STATIC but has no `static_value`"));

                switch (cfg_.role(i).type())
                {
                    case protobuf::QueuedMessageEntry::DESTINATION_ID:
                        static_meta_.set_dest(cfg_.role(i).static_value());
                        break;

                    case protobuf::QueuedMessageEntry::SOURCE_ID:
                        static_meta_.set_src(cfg_.role(i).static_value());
                        break;

                    case protobuf::QueuedMessageEntry::TIMESTAMP:
                        throw(QueueException("TIMESTAMP role cannot be static"));
                        break;
                }
            }
            break;

            case protobuf::QueuedMessageEntry::Role::FIELD_VALUE:
            {
                role_field = cfg_.role(i).field();

                // check that the FIELD_VALUE roles fields actually exist
                find_queue_field(role_field, *new_msg);
            }
            break;
        }
        typedef std::map<protobuf::QueuedMessageEntry::RoleType, std::string> Map;

        std::pair<Map::iterator, bool> result =
            roles_.insert(std::make_pair(cfg_.role(i).type(), role_field));
        if (!result.second)
            throw(QueueException("Role " +
                                 protobuf::QueuedMessageEntry::RoleType_Name(cfg_.role(i).type()) +
                                 " was assigned more than once. Each role must have at most one "
                                 "field or static value per message."));
    }
}
