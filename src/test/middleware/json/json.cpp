// Copyright 2019-2021:
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

#define BOOST_TEST_MODULE json_test
#include <boost/test/included/unit_test.hpp>

#include "goby/middleware/marshalling/json.h"
#include "goby/util/debug_logger.h"

using goby::middleware::SerializerParserHelper;
using json = nlohmann::json;

struct GlogConfig
{
    GlogConfig()
    {
        goby::glog.add_stream(goby::util::logger::DEBUG3, &std::cerr);
        goby::glog.set_name("json");
    }
    ~GlogConfig() = default;
};

BOOST_GLOBAL_FIXTURE(GlogConfig);

json run_serialize_parse(const json& packet_in)
{
    std::cout << "In: " << packet_in.dump() << std::endl;

    auto bytes =
        SerializerParserHelper<json, goby::middleware::scheme<json>()>::serialize(packet_in);

    std::cout << "Bytes: ";
    for (int c : bytes)
        std::cout << std::setfill('0') << std::setw(2) << std::hex << (c & 0xFF) << " ";
    std::cout << std::endl;

    auto bytes_begin = bytes.begin(), bytes_end = bytes.end(), actual_end = bytes.begin();
    auto packet_out = SerializerParserHelper<json, goby::middleware::scheme<json>()>::parse(
        bytes_begin, bytes_end, actual_end);
    std::cout << "Out: " << packet_out->dump() << std::endl;
    return *packet_out;
}

BOOST_AUTO_TEST_CASE(json_simple)
{
    constexpr auto scheme = goby::middleware::scheme<json>();

    BOOST_REQUIRE_EQUAL(scheme, goby::middleware::MarshallingScheme::JSON);

    std::string name =
        SerializerParserHelper<json, goby::middleware::MarshallingScheme::JSON>::type_name();
    std::string expected_name = "nlohmann::json";
    BOOST_REQUIRE_EQUAL(name, expected_name);

    auto j_in = json::parse(R"({"happy": true, "pi": 3.141})");

    auto j_out = run_serialize_parse(j_in);

    BOOST_CHECK_EQUAL(j_in["happy"], j_out["happy"]);
    BOOST_CHECK_EQUAL(j_in["pi"], j_out["pi"]);
}

// arbitrary type
namespace ns
{
// a simple struct to model a person
struct person
{
    // use 'type' field to indicate type for Goby
    static constexpr auto goby_json_type = "person";
    std::string name;
    std::string address;
    int age;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(person, name, address, age)

struct person2
{
    std::string name;
    std::string address;
    int age;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(person2, name, address, age)
} // namespace ns

namespace goby
{
namespace middleware
{
// use template specialization to indicate type field for goby
template <> constexpr const char* json_type_name<ns::person2>() { return "person2"; }
} // namespace middleware
} // namespace goby

BOOST_AUTO_TEST_CASE(json_arbitrary_person)
{
    ns::person p_in{"Ned Flanders", "744 Evergreen Terrace", 60};
    ns::person2 p2_in{"Ned Flanders2", "744 Evergreen Terrace", 61};

    auto bytes =
        SerializerParserHelper<ns::person, goby::middleware::scheme<ns::person>()>::serialize(p_in);
    auto bytes2 =
        SerializerParserHelper<ns::person2, goby::middleware::MarshallingScheme::JSON>::serialize(
            p2_in);

    std::cout << "Bytes: ";
    for (int c : bytes)
        std::cout << std::setfill('0') << std::setw(2) << std::hex << (c & 0xFF) << " ";
    std::cout << std::endl;

    auto bytes_begin = bytes.begin(), bytes_end = bytes.end(), actual_end = bytes.begin();
    auto p_out =
        SerializerParserHelper<ns::person, goby::middleware::MarshallingScheme::JSON>::parse(
            bytes_begin, bytes_end, actual_end);

    auto bytes2_begin = bytes2.begin(), bytes2_end = bytes2.end(), actual2_end = bytes2.begin();
    auto p2_out =
        SerializerParserHelper<ns::person2, goby::middleware::MarshallingScheme::JSON>::parse(
            bytes2_begin, bytes2_end, actual2_end);

    std::string person_name =
        SerializerParserHelper<ns::person, goby::middleware::MarshallingScheme::JSON>::type_name();
    std::cout << "Person Name: " << person_name << std::endl;
    std::string person2_name =
        SerializerParserHelper<ns::person2, goby::middleware::MarshallingScheme::JSON>::type_name();
    std::cout << "Person2 Name: " << person2_name << std::endl;

    BOOST_CHECK_EQUAL(person_name, "person");
    BOOST_CHECK_EQUAL(person2_name, "person2");

    BOOST_CHECK_EQUAL(p_in.name, p_out->name);
    BOOST_CHECK_EQUAL(p_in.address, p_out->address);
    BOOST_CHECK_EQUAL(p_in.age, p_out->age);
}
