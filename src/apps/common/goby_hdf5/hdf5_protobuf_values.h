// Copyright 2009-2018 Toby Schneider (http://gobysoft.org/index.wt/people/toby)
//                     GobySoft, LLC (2013-)
//                     Massachusetts Institute of Technology (2007-2014)
//
//
// This file is part of the Goby Underwater Autonomy Project Binaries
// ("The Goby Binaries").
//
// The Goby Binaries are free software: you can redistribute them and/or modify
// them under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 2 of the License, or
// (at your option) any later version.
//
// The Goby Binaries are distributed in the hope that they will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with Goby.  If not, see <http://www.gnu.org/licenses/>.

#ifndef GOBYHDF5PROTOBUFVALUES20160525H
#define GOBYHDF5PROTOBUFVALUES20160525H

#include <google/protobuf/descriptor.h>
#include <google/protobuf/message.h>

namespace goby
{
namespace common
{
namespace hdf5
{
struct PBMeta
{
    PBMeta(const google::protobuf::Reflection* r, const google::protobuf::FieldDescriptor* f,
           const google::protobuf::Message& m)
        : refl(r), field_desc(f), msg(m)
    {
    }

    const google::protobuf::Reflection* refl;
    const google::protobuf::FieldDescriptor* field_desc;
    const google::protobuf::Message& msg;
};

template <typename T>
void retrieve_default_value(T* val, const google::protobuf::FieldDescriptor* field_desc);

template <typename T> void retrieve_empty_value(T* val);
template <typename T> T retrieve_empty_value()
{
    T t;
    retrieve_empty_value(&t);
    return t;
}

template <typename T> void retrieve_single_value(T* val, PBMeta m)
{
    if (m.field_desc->is_optional() && !m.refl->HasField(m.msg, m.field_desc))
        retrieve_empty_value(val);
    else
        retrieve_single_present_value(val, m);
}

template <typename T> void retrieve_single_present_value(T* val, PBMeta meta);
template <typename T> void retrieve_repeated_value(T* val, int index, PBMeta meta);

template <>
void retrieve_default_value(std::int32_t* val, const google::protobuf::FieldDescriptor* field_desc)
{
    if (field_desc->cpp_type() == google::protobuf::FieldDescriptor::CPPTYPE_INT32)
    {
        *val = field_desc->default_value_int32();
    }
    else if (field_desc->cpp_type() == google::protobuf::FieldDescriptor::CPPTYPE_ENUM)
    {
        const google::protobuf::EnumValueDescriptor* enum_desc = field_desc->default_value_enum();
        *val = enum_desc->number();
    }
}
template <> void retrieve_empty_value(std::int32_t* val)
{
    *val = std::numeric_limits<std::int32_t>::max();
}
template <> void retrieve_single_present_value(std::int32_t* val, PBMeta m)
{
    if (m.field_desc->cpp_type() == google::protobuf::FieldDescriptor::CPPTYPE_INT32)
    {
        *val = m.refl->GetInt32(m.msg, m.field_desc);
    }
    else if (m.field_desc->cpp_type() == google::protobuf::FieldDescriptor::CPPTYPE_ENUM)
    {
        const google::protobuf::EnumValueDescriptor* enum_desc =
            m.refl->GetEnum(m.msg, m.field_desc);
        *val = enum_desc->number();
    }
}
template <> void retrieve_repeated_value(std::int32_t* val, int index, PBMeta m)
{
    if (m.field_desc->cpp_type() == google::protobuf::FieldDescriptor::CPPTYPE_INT32)
    {
        *val = m.refl->GetRepeatedInt32(m.msg, m.field_desc, index);
    }
    else if (m.field_desc->cpp_type() == google::protobuf::FieldDescriptor::CPPTYPE_ENUM)
    {
        const google::protobuf::EnumValueDescriptor* enum_desc =
            m.refl->GetRepeatedEnum(m.msg, m.field_desc, index);
        *val = enum_desc->number();
    }
}

template <>
void retrieve_default_value(std::uint32_t* val, const google::protobuf::FieldDescriptor* field_desc)
{
    *val = field_desc->default_value_uint32();
}
template <> void retrieve_empty_value(std::uint32_t* val)
{
    *val = std::numeric_limits<std::uint32_t>::max();
}
template <> void retrieve_single_present_value(std::uint32_t* val, PBMeta m)
{
    *val = m.refl->GetUInt32(m.msg, m.field_desc);
}
template <> void retrieve_repeated_value(std::uint32_t* val, int index, PBMeta m)
{
    *val = m.refl->GetRepeatedUInt32(m.msg, m.field_desc, index);
}

template <>
void retrieve_default_value(std::int64_t* val, const google::protobuf::FieldDescriptor* field_desc)
{
    *val = field_desc->default_value_int64();
}
template <> void retrieve_empty_value(std::int64_t* val)
{
    *val = std::numeric_limits<std::int64_t>::max();
}
template <> void retrieve_single_present_value(std::int64_t* val, PBMeta m)
{
    *val = m.refl->GetInt64(m.msg, m.field_desc);
}
template <> void retrieve_repeated_value(std::int64_t* val, int index, PBMeta m)
{
    *val = m.refl->GetRepeatedInt64(m.msg, m.field_desc, index);
}

template <>
void retrieve_default_value(std::uint64_t* val, const google::protobuf::FieldDescriptor* field_desc)
{
    *val = field_desc->default_value_uint64();
}
template <> void retrieve_empty_value(std::uint64_t* val)
{
    *val = std::numeric_limits<std::uint64_t>::max();
}
template <> void retrieve_single_present_value(std::uint64_t* val, PBMeta m)
{
    *val = m.refl->GetUInt64(m.msg, m.field_desc);
}
template <> void retrieve_repeated_value(std::uint64_t* val, int index, PBMeta m)
{
    *val = m.refl->GetRepeatedUInt64(m.msg, m.field_desc, index);
}

template <>
void retrieve_default_value(double* val, const google::protobuf::FieldDescriptor* field_desc)
{
    *val = field_desc->default_value_double();
}
template <> void retrieve_empty_value(double* val)
{
    *val = std::numeric_limits<double>::quiet_NaN();
}
template <> void retrieve_single_present_value(double* val, PBMeta m)
{
    *val = m.refl->GetDouble(m.msg, m.field_desc);
}
template <> void retrieve_repeated_value(double* val, int index, PBMeta m)
{
    *val = m.refl->GetRepeatedDouble(m.msg, m.field_desc, index);
}

template <>
void retrieve_default_value(float* val, const google::protobuf::FieldDescriptor* field_desc)
{
    *val = field_desc->default_value_float();
}
template <> void retrieve_empty_value(float* val)
{
    *val = std::numeric_limits<float>::quiet_NaN();
}
template <> void retrieve_single_present_value(float* val, PBMeta m)
{
    *val = m.refl->GetFloat(m.msg, m.field_desc);
}
template <> void retrieve_repeated_value(float* val, int index, PBMeta m)
{
    *val = m.refl->GetRepeatedFloat(m.msg, m.field_desc, index);
}

// used for bool
template <>
void retrieve_default_value(unsigned char* val, const google::protobuf::FieldDescriptor* field_desc)
{
    if (field_desc->cpp_type() == google::protobuf::FieldDescriptor::CPPTYPE_BOOL)
        *val = field_desc->default_value_bool();
}
template <> void retrieve_empty_value(unsigned char* val)
{
    *val = std::numeric_limits<unsigned char>::max();
}
template <> void retrieve_single_present_value(unsigned char* val, PBMeta m)
{
    if (m.field_desc->cpp_type() == google::protobuf::FieldDescriptor::CPPTYPE_BOOL)
        *val = m.refl->GetBool(m.msg, m.field_desc);
}
template <> void retrieve_repeated_value(unsigned char* val, int index, PBMeta m)
{
    if (m.field_desc->cpp_type() == google::protobuf::FieldDescriptor::CPPTYPE_BOOL)
        *val = m.refl->GetRepeatedBool(m.msg, m.field_desc, index);
}

template <>
void retrieve_default_value(std::string* val, const google::protobuf::FieldDescriptor* field_desc)
{
    *val = field_desc->default_value_string();
    if (field_desc->type() == google::protobuf::FieldDescriptor::TYPE_BYTES)
        *val = goby::util::hex_encode(*val);
}
template <> void retrieve_empty_value(std::string* val) { val->clear(); }
template <> void retrieve_single_value(std::string* val, PBMeta m)
{
    *val = m.refl->GetString(m.msg, m.field_desc);
    if (m.field_desc->type() == google::protobuf::FieldDescriptor::TYPE_BYTES)
        *val = goby::util::hex_encode(*val);
}
template <> void retrieve_repeated_value(std::string* val, int index, PBMeta m)
{
    *val = m.refl->GetRepeatedString(m.msg, m.field_desc, index);
    if (m.field_desc->type() == google::protobuf::FieldDescriptor::TYPE_BYTES)
        *val = goby::util::hex_encode(*val);
}

} // namespace hdf5
} // namespace common
} // namespace goby

#endif
