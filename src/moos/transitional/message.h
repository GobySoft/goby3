// Copyright 2011-2020:
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

#ifndef MESSAGE20091211H
#define MESSAGE20091211H

#include <algorithm>
#include <bitset>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <vector>

#include <memory>

#include "goby/acomms/protobuf/queue.pb.h"

#include "goby/util/as.h"
#include "message_var.h"
// #include "message_var_bool.h"
// #include "message_var_enum.h"
// #include "message_var_float.h"
// #include "message_var_head.h"
// #include "message_var_hex.h"
// #include "message_var_int.h"
// #include "message_var_static.h"
// #include "message_var_string.h"

#include "dccl_constants.h"
#include "message_publish.h"

namespace goby
{
namespace moos
{
namespace transitional
{
// the DCCLMessage itself
class DCCLMessage
{
  public:
    // Added in Goby2 for transition to new Protobuf structure
    void write_schema_to_dccl2(std::ofstream* proto_file);

    DCCLMessage();

    // set
    void set_name(const std::string& name) { name_ = name; }

    void set_id(unsigned id) { id_ = id; }
    template <typename T> void set_id(const T& t) { set_id(util::as<unsigned>(t)); }

    void set_trigger(const std::string& trigger_type) { trigger_type_ = trigger_type; }

    void set_trigger_var(const std::string& trigger_var) { trigger_var_ = trigger_var; }

    void set_trigger_time(double trigger_time) { trigger_time_ = trigger_time; }
    template <typename T> void set_trigger_time(const T& t)
    {
        set_trigger_time(util::as<double>(t));
    }

    void set_trigger_mandatory(const std::string& trigger_mandatory)
    {
        trigger_mandatory_ = trigger_mandatory;
    }

    void set_in_var(const std::string& in_var) { in_var_ = in_var; }

    void set_out_var(const std::string& out_var) { out_var_ = out_var; }

    void set_size(unsigned size) { size_ = size; }

    template <typename T> void set_size(const T& t) { set_size(util::as<unsigned>(t)); }

    void set_repeat_enabled(unsigned repeat_enabled) { repeat_enabled_ = repeat_enabled; }
    template <typename T> void set_repeat_enabled(const T& t)
    {
        set_repeat_enabled(util::as<unsigned>(t));
    }

    void add_message_var(const std::string& type);
    void add_publish();

    void set_descriptor(const google::protobuf::Descriptor* descriptor)
    {
        descriptor_ = descriptor;
    }

    //get
    std::string name() const { return name_; }
    unsigned id() const { return id_; }
    unsigned size() const { return size_; }
    std::string trigger_var() const { return trigger_var_; }
    std::string trigger_mandatory() const { return trigger_mandatory_; }
    double trigger_time() const { return trigger_time_; }
    unsigned trigger_number() const { return trigger_number_; }
    std::string trigger_type() const { return trigger_type_; }
    std::string in_var() const { return in_var_; }
    std::string out_var() const { return out_var_; }
    bool repeat_enabled() const { return repeat_enabled_; }
    unsigned repeat() const { return repeat_; }

    DCCLMessageVar& last_message_var() { return *layout_.back(); }
    DCCLMessageVar& header_var(moos::transitional::DCCLHeaderPart p) { return *header_[p]; }
    DCCLPublish& last_publish() { return publishes_.back(); }

    std::vector<std::shared_ptr<DCCLMessageVar> >& layout() { return layout_; }
    std::vector<std::shared_ptr<DCCLMessageVar> >& header() { return header_; }

    const std::vector<std::shared_ptr<DCCLMessageVar> >& layout_const() const { return layout_; }
    const std::vector<std::shared_ptr<DCCLMessageVar> >& header_const() const { return header_; }

    std::vector<DCCLPublish>& publishes() { return publishes_; }
    const std::vector<DCCLPublish>& publishes_const() const { return publishes_; }

    std::shared_ptr<DCCLMessageVar> name2message_var(const std::string& name) const;

    const google::protobuf::Descriptor* descriptor() { return descriptor_; }

    //other
    /* std::string get_display() const; */
    /* std::string get_short_display() const; */
    std::map<std::string, std::string> message_var_names() const;
    void preprocess();
    void set_repeat_array_length();
    unsigned calc_total_size();

    void set_head_defaults(std::map<std::string, std::vector<DCCLMessageVal> >& in,
                           unsigned modem_id);

    // increment, means increase trigger number
    // prefix ++DCCLMessage
    DCCLMessage& operator++()
    {
        ++trigger_number_;
        return (*this);
    }
    // postfix DCCLMessage++
    const DCCLMessage operator++(int)
    {
        DCCLMessage tmp(*this);
        ++(*this);
        return tmp;
    }

  private:
  private:
    // total request size of message, e.g. 32 bytes
    unsigned size_;

    unsigned trigger_number_;
    // actual used bits in body part of message (not including header bits)
    // unsigned body_bits_;

    unsigned id_;
    unsigned modem_id_;
    double trigger_time_;

    bool repeat_enabled_;
    unsigned repeat_;

    std::string name_;
    std::string trigger_type_;
    std::string trigger_var_;
    std::string trigger_mandatory_;
    std::string in_var_;
    std::string out_var_;

    std::vector<std::shared_ptr<DCCLMessageVar> > layout_;
    std::vector<std::shared_ptr<DCCLMessageVar> > header_;

    std::vector<DCCLPublish> publishes_;

    const google::protobuf::Descriptor* descriptor_;
};

/* inline void bitset2string(const boost::dynamic_bitset<unsigned char>& body_bits, */
/*                           std::string& body) */
/* { */
/*     body.resize(body_bits.num_blocks()); // resize the string to fit the bitset */
/*     to_block_range(body_bits, body.rbegin()); */
/* } */

/* inline void string2bitset(boost::dynamic_bitset<unsigned char>& body_bits, */
/*                           const std::string& body) */
/* { */
/*     from_block_range(body.rbegin(), body.rend(), body_bits); */
/* } */

//        std::ostream& operator<< (std::ostream& out, const DCCLMessage& message);
} // namespace transitional
} // namespace moos
} // namespace goby
#endif
