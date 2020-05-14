// Copyright 2019-2020:
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

#define BOOST_TEST_MODULE mavlink_test
#include <boost/test/included/unit_test.hpp>

#include "goby/middleware/marshalling/mavlink.h"
#include "goby/util/binary.h"

using goby::middleware::SerializerParserHelper;

struct GlogConfig
{
    GlogConfig()
    {
        goby::glog.add_stream(goby::util::logger::DEBUG3, &std::cerr);
        goby::glog.set_name("mavlink");
    }
    ~GlogConfig() {}
};

BOOST_GLOBAL_FIXTURE(GlogConfig);

template <typename MAVLinkMessage>
MAVLinkMessage run_serialize_parse(const MAVLinkMessage& packet_in)
{
    std::cout << "In: " << packet_in.to_yaml() << std::endl;

    auto bytes =
        SerializerParserHelper<MAVLinkMessage,
                               goby::middleware::scheme<MAVLinkMessage>()>::serialize(packet_in);

    auto bytes_begin = bytes.begin(), bytes_end = bytes.end(), actual_end = bytes.begin();
    auto packet_out =
        SerializerParserHelper<MAVLinkMessage, goby::middleware::scheme<MAVLinkMessage>()>::parse(
            bytes_begin, bytes_end, actual_end);
    std::cout << "Out: " << packet_out->to_yaml() << std::endl;
    return *packet_out;
}

template <typename MAVLinkMessage>
std::tuple<int, int, MAVLinkMessage> run_serialize_parse_with_metadata(
    const std::tuple<int, int, MAVLinkMessage>& packet_in_with_metadata)
{
    auto write_tuple =
        [](const std::tuple<int, int, MAVLinkMessage>& packet_with_metadata) -> std::string {
        return "sysid: " +
               std::to_string(std::get<goby::middleware::MAVLinkTupleIndices::SYSTEM_ID_INDEX>(
                   packet_with_metadata)) +
               ", compid: " +
               std::to_string(std::get<goby::middleware::MAVLinkTupleIndices::COMPONENT_ID_INDEX>(
                   packet_with_metadata)) +
               ", " +
               std::get<goby::middleware::MAVLinkTupleIndices::PACKET_INDEX>(packet_with_metadata)
                   .to_yaml();
    };

    std::cout << "In: " << write_tuple(packet_in_with_metadata) << std::endl;

    auto bytes =
        SerializerParserHelper<std::tuple<int, int, MAVLinkMessage>,
                               goby::middleware::scheme<std::tuple<int, int, MAVLinkMessage>>()>::
            serialize(packet_in_with_metadata);

    auto bytes_begin = bytes.begin(), bytes_end = bytes.end(), actual_end = bytes.begin();
    auto packet_out_with_metadata = SerializerParserHelper<
        std::tuple<int, int, MAVLinkMessage>,
        goby::middleware::scheme<std::tuple<int, int, MAVLinkMessage>>()>::parse(bytes_begin,
                                                                                 bytes_end,
                                                                                 actual_end);

    std::cout << "Out: " << write_tuple(*packet_out_with_metadata) << std::endl;
    return *packet_out_with_metadata;
}

BOOST_AUTO_TEST_CASE(mavlink_common_heartbeat)
{
    constexpr auto scheme = goby::middleware::scheme<mavlink::common::msg::HEARTBEAT>();

    BOOST_REQUIRE_EQUAL(scheme, goby::middleware::MarshallingScheme::MAVLINK);

    // from mavlink's gtestsuite.hpp
    mavlink::common::msg::HEARTBEAT packet_in{};
    packet_in.type = 17;
    packet_in.autopilot = 84;
    packet_in.base_mode = 151;
    packet_in.custom_mode = 963497464;
    packet_in.system_status = 218;

    auto packet_out = run_serialize_parse(packet_in);

    BOOST_CHECK_EQUAL(packet_in.type, packet_out.type);
    BOOST_CHECK_EQUAL(packet_in.autopilot, packet_out.autopilot);
    BOOST_CHECK_EQUAL(packet_in.base_mode, packet_out.base_mode);
    BOOST_CHECK_EQUAL(packet_in.custom_mode, packet_out.custom_mode);
    BOOST_CHECK_EQUAL(packet_in.system_status, packet_out.system_status);
    //    BOOST_CHECK_EQUAL(packet_in.mavlink_version, packet_out.mavlink_version);

    auto packet_out_with_metadata =
        run_serialize_parse_with_metadata(std::make_tuple(2, 3, packet_in));
    BOOST_CHECK_EQUAL(
        std::get<goby::middleware::MAVLinkTupleIndices::SYSTEM_ID_INDEX>(packet_out_with_metadata),
        2);
    BOOST_CHECK_EQUAL(std::get<goby::middleware::MAVLinkTupleIndices::COMPONENT_ID_INDEX>(
                          packet_out_with_metadata),
                      3);
    BOOST_CHECK_EQUAL(
        std::get<goby::middleware::MAVLinkTupleIndices::PACKET_INDEX>(packet_out_with_metadata)
            .to_yaml(),
        packet_out.to_yaml());
}

BOOST_AUTO_TEST_CASE(mavlink_common_sys_status)
{
    mavlink::common::msg::SYS_STATUS packet_in{};
    packet_in.onboard_control_sensors_present = 963497464;
    packet_in.onboard_control_sensors_enabled = 963497672;
    packet_in.onboard_control_sensors_health = 963497880;
    packet_in.load = 17859;
    packet_in.voltage_battery = 17963;
    packet_in.current_battery = 18067;
    packet_in.battery_remaining = -33;
    packet_in.drop_rate_comm = 18171;
    packet_in.errors_comm = 18275;
    packet_in.errors_count1 = 18379;
    packet_in.errors_count2 = 18483;
    packet_in.errors_count3 = 18587;
    packet_in.errors_count4 = 18691;

    auto packet_out = run_serialize_parse(packet_in);

    BOOST_CHECK_EQUAL(packet_in.onboard_control_sensors_present,
                      packet_out.onboard_control_sensors_present);
    BOOST_CHECK_EQUAL(packet_in.onboard_control_sensors_enabled,
                      packet_out.onboard_control_sensors_enabled);
    BOOST_CHECK_EQUAL(packet_in.onboard_control_sensors_health,
                      packet_out.onboard_control_sensors_health);
    BOOST_CHECK_EQUAL(packet_in.load, packet_out.load);
    BOOST_CHECK_EQUAL(packet_in.voltage_battery, packet_out.voltage_battery);
    BOOST_CHECK_EQUAL(packet_in.current_battery, packet_out.current_battery);
    BOOST_CHECK_EQUAL(packet_in.battery_remaining, packet_out.battery_remaining);
    BOOST_CHECK_EQUAL(packet_in.drop_rate_comm, packet_out.drop_rate_comm);
    BOOST_CHECK_EQUAL(packet_in.errors_comm, packet_out.errors_comm);
    BOOST_CHECK_EQUAL(packet_in.errors_count1, packet_out.errors_count1);
    BOOST_CHECK_EQUAL(packet_in.errors_count2, packet_out.errors_count2);
    BOOST_CHECK_EQUAL(packet_in.errors_count3, packet_out.errors_count3);
    BOOST_CHECK_EQUAL(packet_in.errors_count4, packet_out.errors_count4);
}

// test non-standard message
#include "mavlink/v2.0/ardupilotmega/ardupilotmega.hpp"
BOOST_AUTO_TEST_CASE(mavlink_ardupilot_mega)
{
    goby::middleware::MAVLinkRegistry::register_dialect_entries(
        mavlink::ardupilotmega::MESSAGE_ENTRIES);

    mavlink::ardupilotmega::msg::RPM packet_in{};
    packet_in.rpm1 = 17.0;
    packet_in.rpm2 = 45.0;

    auto packet_out = run_serialize_parse(packet_in);

    BOOST_CHECK_EQUAL(packet_in.rpm1, packet_out.rpm1);
    BOOST_CHECK_EQUAL(packet_in.rpm2, packet_out.rpm2);
}
