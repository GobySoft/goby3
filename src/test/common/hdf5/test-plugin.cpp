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

#include "goby/common/hdf5_plugin.h"

#include "goby/common/time.h"
#include "goby/util/binary.h"

#include "test.pb.h"
#include "test2.pb.h"

class TestHDF5Plugin : public goby::common::HDF5Plugin
{
  public:
    TestHDF5Plugin(const goby::common::protobuf::HDF5Config* cfg);

  private:
    bool provide_entry(goby::common::HDF5ProtobufEntry* entry);
    void fill_message(TestMsg& msg_in);
    void fill_message(TestHDF5Message& msg);

  private:
};

extern "C"
{
    goby::common::HDF5Plugin* goby_hdf5_load(const goby::common::protobuf::HDF5Config* cfg)
    {
        return new TestHDF5Plugin(cfg);
    }
}

TestHDF5Plugin::TestHDF5Plugin(const goby::common::protobuf::HDF5Config* cfg)
    : goby::common::HDF5Plugin(cfg)
{
}

bool TestHDF5Plugin::provide_entry(goby::common::HDF5ProtobufEntry* entry)
{
    static int entry_index = 0;

    if (entry_index > 20)
        return false;

    if (entry_index < 3 || entry_index > 7)
    {
        TestMsg msg;
        fill_message(msg);
        entry->msg = std::shared_ptr<google::protobuf::Message>(new TestMsg(msg));
    }
    else
    {
        TestHDF5Message msg;
        fill_message(msg);
        entry->msg = std::shared_ptr<google::protobuf::Message>(new TestHDF5Message(msg));
    }

    if (entry_index < 10)
        entry->channel =
            "\t/test/group1"; // add some whitespace and leading "/" - will become "test/group1"
    else
        entry->channel =
            " test/group2/"; // add some whitespace and trailing "/" - will become "test/group2"

    entry->time = goby::time::now();

    std::cout << *entry << std::endl;

    ++entry_index;

    return true;
}

void TestHDF5Plugin::fill_message(TestHDF5Message& msg)
{
    static int i = 0;
    static int i2 = 0;
    static int i3 = 0;
    static int i4 = 0;
    static int i5 = 0;
    static int i6 = 0;
    if (i == 0)
    {
        for (int j = 0; j < 10; ++j) msg.add_a(++i);

        for (int j = 0; j < 10; ++j)
        {
            B* b = msg.add_b();
            for (int k = 0; k < 20; ++k)
            {
                b->add_c(++i2);
                if (k < 10)
                    b->add_d(++i3);
                if (k < 5)
                    b->add_e(++i4);
            }
            for (int l = 0; l < 3; ++l)
            {
                F* f = b->add_f();
                f->set_h(++i5);
                for (int m = 0; m < 6; ++m) f->add_g(++i6);
            }
        }
    }
    else
    {
        for (int j = 0; j < 20; ++j) msg.add_a(++i);

        for (int j = 0; j < 3; ++j)
        {
            B* b = msg.add_b();
            for (int k = 0; k < 2; ++k)
            {
                b->add_c(++i2);
                b->add_d(++i3);
                b->add_e(++i4);
            }
            for (int l = 0; l < 5; ++l)
            {
                F* f = b->add_f();
                f->set_h(++i5);
                for (int m = 0; m < 8; ++m) f->add_g(++i6);
            }
        }
    }
}

void TestHDF5Plugin::fill_message(TestMsg& msg_in)
{
    static int i = 0;
    msg_in.set_double_default_optional(++i + 0.1);
    msg_in.set_float_default_optional(++i + 0.2);

    msg_in.set_int32_default_optional(++i);
    msg_in.set_int64_default_optional(-++i);
    msg_in.set_uint32_default_optional(++i);
    msg_in.set_uint64_default_optional(++i);
    msg_in.set_sint32_default_optional(-++i);
    msg_in.set_sint64_default_optional(++i);
    msg_in.set_fixed32_default_optional(++i);
    msg_in.set_fixed64_default_optional(++i);
    msg_in.set_sfixed32_default_optional(++i);
    msg_in.set_sfixed64_default_optional(-++i);

    msg_in.set_bool_default_optional(true);

    msg_in.set_string_default_optional("abc123");
    msg_in.set_bytes_default_optional(goby::util::hex_decode("00112233aabbcc1234"));

    msg_in.set_enum_default_optional(ENUM_C);
    msg_in.mutable_msg_default_optional()->set_val(++i + 0.3);
    msg_in.mutable_msg_default_optional()->mutable_msg()->set_val(++i);

    msg_in.set_double_default_required(++i + 0.1);
    msg_in.set_float_default_required(++i + 0.2);

    msg_in.set_int32_default_required(++i);
    msg_in.set_int64_default_required(-++i);
    msg_in.set_uint32_default_required(++i);
    msg_in.set_uint64_default_required(++i);
    msg_in.set_sint32_default_required(-++i);
    msg_in.set_sint64_default_required(++i);
    msg_in.set_fixed32_default_required(++i);
    msg_in.set_fixed64_default_required(++i);
    msg_in.set_sfixed32_default_required(++i);
    msg_in.set_sfixed64_default_required(-++i);

    msg_in.set_bool_default_required(true);

    msg_in.set_string_default_required("abc123");
    msg_in.set_bytes_default_required(goby::util::hex_decode("00112233aabbcc1234"));

    msg_in.set_enum_default_required(ENUM_C);
    msg_in.mutable_msg_default_required()->set_val(++i + 0.3);
    msg_in.mutable_msg_default_required()->mutable_msg()->set_val(++i);

    for (int j = 0; j < 3; ++j)
    {
        msg_in.add_double_default_repeat(++i + 0.1);
        msg_in.add_float_default_repeat(++i + 0.2);

        msg_in.add_int32_default_repeat(++i);
        msg_in.add_int64_default_repeat(-++i);
        msg_in.add_uint32_default_repeat(++i);
        msg_in.add_uint64_default_repeat(++i);
        msg_in.add_sint32_default_repeat(-++i);
        msg_in.add_sint64_default_repeat(++i);
        msg_in.add_fixed32_default_repeat(++i);
        msg_in.add_fixed64_default_repeat(++i);
        msg_in.add_sfixed32_default_repeat(++i);
        msg_in.add_sfixed64_default_repeat(-++i);

        msg_in.add_bool_default_repeat(true);

        msg_in.add_string_default_repeat("abc123");

        if (j)
            msg_in.add_bytes_default_repeat(goby::util::hex_decode("00aabbcc"));
        else
            msg_in.add_bytes_default_repeat(goby::util::hex_decode("ffeedd12"));

        msg_in.add_enum_default_repeat(static_cast<Enum1>((++i % 3) + 1));
        EmbeddedMsg1* em_msg = msg_in.add_msg_default_repeat();
        em_msg->set_val(++i + 0.3);
        em_msg->mutable_msg()->set_val(++i);
    }
}
