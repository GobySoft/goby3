// Copyright 2025:
//   GobySoft, LLC (2013-)
//   Community contributors (see AUTHORS file)
// File authors:
//   Toby Schneider <toby@gobysoft.org>
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

#define BOOST_TEST_MODULE dccl_load_library_test
#include <boost/test/included/unit_test.hpp>

#include "goby/exception.h"
#include "goby/middleware/marshalling/dccl.h"
#include "goby/util/debug_logger.h"

#include "goby/test/middleware/dccl_load_library/test.pb.h"

using goby::middleware::SerializerParserHelper;
using goby::test::middleware::protobuf::NativeProtobufSample;
using Helper =
    SerializerParserHelper<NativeProtobufSample, goby::middleware::MarshallingScheme::DCCL>;

struct GlogConfig
{
    GlogConfig()
    {
        goby::glog.add_stream(goby::util::logger::DEBUG3, &std::cerr);
        goby::glog.set_name("dccl_load_library");
    }
    ~GlogConfig() = default;
};

BOOST_GLOBAL_FIXTURE(GlogConfig);

// a library that doesn't exist must throw rather than silently do nothing
BOOST_AUTO_TEST_CASE(load_nonexistent_library)
{
    BOOST_CHECK_THROW(goby::middleware::detail::DCCLSerializerParserHelperBase::load_library(
                          "libgoby_this_library_does_not_exist.so"),
                      goby::Exception);
}

// Library loading is process-wide and permanent, so the "before" and "after" checks
// must live in a single test case to be independent of test ordering.
BOOST_AUTO_TEST_CASE(codec_available_only_after_load)
{
    // without the external DCCL plugin library loaded, the message cannot be used
    BOOST_CHECK_THROW(Helper::id(), dccl::Exception);

    goby::middleware::detail::DCCLSerializerParserHelperBase::load_library(
        DCCL_NATIVE_PROTOBUF_LIBRARY);

    // loading the same library twice is a no-op, and empty entries (from the trailing
    // delimiter) are skipped rather than passed to dlopen
    goby::middleware::detail::DCCLSerializerParserHelperBase::load_library(
        std::string(DCCL_NATIVE_PROTOBUF_LIBRARY) + ":");

    BOOST_CHECK_EQUAL(Helper::id(), 126);

    NativeProtobufSample msg_in;
    msg_in.set_a(10);
    msg_in.set_b(3.5);

    auto bytes = Helper::serialize(msg_in);

    auto bytes_begin = bytes.begin(), bytes_end = bytes.end(), actual_end = bytes.begin();
    auto msg_out = Helper::parse(bytes_begin, bytes_end, actual_end);

    BOOST_CHECK_EQUAL(msg_in.SerializeAsString(), msg_out->SerializeAsString());
}
