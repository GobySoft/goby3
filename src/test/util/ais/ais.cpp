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

#define BOOST_TEST_MODULE ais_codec_test
#include <boost/test/floating_point_comparison.hpp>
#include <boost/test/included/unit_test.hpp>
#include <boost/units/io.hpp>
#include <boost/units/systems/si/prefixes.hpp>

#include <google/protobuf/text_format.h>

#include <iomanip>
#include <iostream>

#include "goby/util/ais.h"

using namespace boost::units;

using goby::util::NMEASentence;
using goby::util::ais::Decoder;
using goby::util::ais::Encoder;

// decode test cases from https://fossies.org/linux/gpsd/test/sample.aivdm

bool close_enough(double a, double b, int precision)
{
    return std::abs(a - b) < std::pow(10, -precision);
}

constexpr double eps_pct{0.001};

BOOST_AUTO_TEST_CASE(ais_decode_5)
{
    std::vector<NMEASentence> nmeas{
        {"!AIVDM,2,1,1,A,55?MbV02;H;s<HtKR20EHE:0@T4@Dn2222222216L961O5Gf0NSQEp6ClRp8,0*1C"},
        {"!AIVDM,2,2,1,A,88888888880,2*25"}};

    std::cout << "IN: " << nmeas[0].message() << std::endl;
    std::cout << "IN: " << nmeas[1].message() << std::endl;

    Decoder decoder(nmeas);
    BOOST_REQUIRE(decoder.complete());
    BOOST_CHECK_EQUAL(decoder.message_id(), 5);
    BOOST_CHECK_EQUAL(decoder.parsed_type(), Decoder::ParsedType::VOYAGE);

    auto voy = decoder.as_voyage();
    std::cout << "OUT: " << voy.ShortDebugString() << std::endl;

    BOOST_CHECK_EQUAL(voy.message_id(), 5);
    BOOST_CHECK_EQUAL(voy.mmsi(), 351759000);
    BOOST_CHECK_EQUAL(voy.imo(), 9134270);
    BOOST_CHECK_EQUAL(voy.name(), "EVER DIADEM");
    BOOST_CHECK_EQUAL(voy.callsign(), "3FOF8");
    BOOST_CHECK_EQUAL(voy.type(), 70);
    BOOST_CHECK_EQUAL(voy.to_bow(), 225);
    BOOST_CHECK_EQUAL(voy.to_stern(), 70);
    BOOST_CHECK_EQUAL(voy.to_port(), 1);
    BOOST_CHECK_EQUAL(voy.to_starboard(), 31);

    BOOST_CHECK_EQUAL(voy.fix_type(), 1);
    BOOST_CHECK_EQUAL(voy.eta_month(), 5);
    BOOST_CHECK_EQUAL(voy.eta_day(), 15);
    BOOST_CHECK_EQUAL(
        voy.eta_hour(),
        14); // using https://www.maritec.co.za/aisvdmvdodecoding1.php, differs from gpsd sample
    BOOST_CHECK_EQUAL(voy.eta_minute(), 0); // https://www.maritec.co.za/aisvdmvdodecoding1.php
    BOOST_CHECK_CLOSE(voy.draught(), 12.2, eps_pct);

    BOOST_CHECK_EQUAL(voy.destination(), "NEW YORK");
}

BOOST_AUTO_TEST_CASE(ais_decode_18_1)
{
    NMEASentence nmea("!AIVDM,1,1,,A,B52K>;h00Fc>jpUlNV@ikwpUoP06,0*4C");
    std::cout << "IN: " << nmea.message() << std::endl;

    Decoder decoder(nmea);
    BOOST_REQUIRE(decoder.complete());
    BOOST_CHECK_EQUAL(decoder.message_id(), 18);
    BOOST_CHECK_EQUAL(decoder.parsed_type(), Decoder::ParsedType::POSITION);

    auto pos = decoder.as_position();
    std::cout << "OUT: " << pos.ShortDebugString() << std::endl;

    BOOST_CHECK_EQUAL(pos.message_id(), 18);
    BOOST_CHECK_EQUAL(pos.mmsi(), 338087471);
    metric::knot_base_unit::unit_type knots;
    BOOST_CHECK_CLOSE(
        pos.speed_over_ground_with_units<quantity<metric::knot_base_unit::unit_type>>().value(),
        0.1, eps_pct);
    BOOST_CHECK_CLOSE(pos.lat(), 40.68454, eps_pct);
    BOOST_CHECK_CLOSE(pos.lon(), -74.07213166666666666666666667, eps_pct);
    BOOST_CHECK_CLOSE(pos.course_over_ground(), 79.6, eps_pct);
    // 511, no true heading
    BOOST_CHECK(!pos.has_true_heading());
    BOOST_CHECK(pos.raim());
}

BOOST_AUTO_TEST_CASE(ais_decode_18_2)
{
    NMEASentence nmea("!AIVDM,1,1,,A,B52KB8h006fu`Q6:g1McCwb5oP06,0*00");
    std::cout << "IN: " << nmea.message() << std::endl;

    Decoder decoder(nmea);
    BOOST_REQUIRE(decoder.complete());
    BOOST_CHECK_EQUAL(decoder.message_id(), 18);
    BOOST_CHECK_EQUAL(decoder.parsed_type(), Decoder::ParsedType::POSITION);

    auto pos = decoder.as_position();
    std::cout << "OUT: " << pos.ShortDebugString() << std::endl;

    BOOST_CHECK_EQUAL(pos.message_id(), 18);
    BOOST_CHECK_EQUAL(pos.mmsi(), 338088483);
    metric::knot_base_unit::unit_type knots;
    BOOST_CHECK_CLOSE(
        pos.speed_over_ground_with_units<quantity<metric::knot_base_unit::unit_type>>().value(), 0,
        eps_pct);
    BOOST_CHECK_CLOSE(pos.lat(), 43.11555833, eps_pct);
    BOOST_CHECK_CLOSE(pos.lon(), -70.8111966, eps_pct);
    BOOST_CHECK_CLOSE(pos.course_over_ground(), 171.6, eps_pct);
    // 511, no true heading
    BOOST_CHECK(!pos.has_true_heading());
    BOOST_CHECK(pos.raim());
}

BOOST_AUTO_TEST_CASE(ais_decode_24_1)
{
    NMEASentence nmea("!AIVDM,1,1,,A,H42O55i18tMET00000000000000,2*6D");
    std::cout << "IN: " << nmea.message() << std::endl;

    Decoder decoder(nmea);
    BOOST_REQUIRE(decoder.complete());
    BOOST_CHECK_EQUAL(decoder.message_id(), 24);
    BOOST_CHECK_EQUAL(decoder.parsed_type(), Decoder::ParsedType::VOYAGE);

    auto voy = decoder.as_voyage();
    std::cout << "OUT: " << voy.ShortDebugString() << std::endl;

    BOOST_CHECK_EQUAL(voy.message_id(), 24);
    BOOST_CHECK_EQUAL(voy.mmsi(), 271041815);
    BOOST_CHECK_EQUAL(voy.name(), "PROGUY");
}

BOOST_AUTO_TEST_CASE(ais_decode_24_2)
{
    NMEASentence nmea("!AIVDM,1,1,,A,H42O55lti4hhhilD3nink000?050,0*40");
    std::cout << "IN: " << nmea.message() << std::endl;

    Decoder decoder(nmea);
    BOOST_REQUIRE(decoder.complete());
    BOOST_CHECK_EQUAL(decoder.message_id(), 24);
    BOOST_CHECK_EQUAL(decoder.parsed_type(), Decoder::ParsedType::VOYAGE);

    auto voy = decoder.as_voyage();
    std::cout << "OUT: " << voy.ShortDebugString() << std::endl;

    BOOST_CHECK_EQUAL(voy.message_id(), 24);
    BOOST_CHECK_EQUAL(voy.mmsi(), 271041815);
    BOOST_CHECK(!voy.has_name());
    BOOST_CHECK_EQUAL(voy.callsign(), "TC6163");
    BOOST_CHECK(voy.type() == goby::util::ais::protobuf::Voyage::TYPE__PASSENGER);
    BOOST_CHECK_EQUAL(voy.to_bow(), 0);
    BOOST_CHECK_EQUAL(voy.to_stern(), 15);
    BOOST_CHECK_EQUAL(voy.to_port(), 0);
    BOOST_CHECK_EQUAL(voy.to_starboard(), 5);
}

BOOST_AUTO_TEST_CASE(ais_encode_18)
{
    goby::util::ais::protobuf::Position pos;
    std::string pos_str(
        "message_id: 18 mmsi: 338087471 speed_over_ground: 0.051444445 lat: 40.68454 lon: "
        "-74.072131666666664 position_accuracy: ACCURACY__LOW__ABOVE_10_METERS "
        "course_over_ground: "
        "79.6 report_second: 49 raim: true");

    google::protobuf::TextFormat::ParseFromString(pos_str, &pos);

    Encoder encoder(pos);

    auto nmeas = encoder.as_nmea();
    BOOST_REQUIRE_EQUAL(nmeas.size(), 1);

    std::cout << "encoder OUT: " << nmeas[0].message() << std::endl;

    Decoder decoder(nmeas);
    BOOST_REQUIRE(decoder.complete());
    BOOST_CHECK_EQUAL(decoder.message_id(), 18);
    BOOST_CHECK_EQUAL(decoder.parsed_type(), Decoder::ParsedType::POSITION);

    auto pos_out = decoder.as_position();
    std::cout << "decoder OUT: " << pos_out.ShortDebugString() << std::endl;

    BOOST_CHECK(pos.SerializeAsString() == pos_out.SerializeAsString());
}

BOOST_AUTO_TEST_CASE(ais_encode_24_1)
{
    goby::util::ais::protobuf::Voyage voy;

    std::string voy_str("message_id: 24 mmsi: 271041815 name: \"PROGUY\"");
    google::protobuf::TextFormat::ParseFromString(voy_str, &voy);

    Encoder encoder(voy, 0);

    auto nmeas = encoder.as_nmea();
    BOOST_REQUIRE_EQUAL(nmeas.size(), 1);

    std::cout << "encoder OUT: " << nmeas[0].message() << std::endl;

    Decoder decoder(nmeas);
    BOOST_REQUIRE(decoder.complete());
    BOOST_CHECK_EQUAL(decoder.message_id(), 24);
    BOOST_CHECK_EQUAL(decoder.parsed_type(), Decoder::ParsedType::VOYAGE);

    auto voy_out = decoder.as_voyage();
    std::cout << "decoder OUT: " << voy_out.ShortDebugString() << std::endl;

    BOOST_CHECK(voy.SerializeAsString() == voy_out.SerializeAsString());
}

BOOST_AUTO_TEST_CASE(ais_encode_24_2)
{
    goby::util::ais::protobuf::Voyage voy;

    std::string voy_str("message_id: 24 mmsi: 271041815 callsign: \"TC6163\" type: TYPE__PASSENGER "
                        "to_bow: 0 to_stern: 15 to_port: 0 to_starboard: 5");
    google::protobuf::TextFormat::ParseFromString(voy_str, &voy);

    Encoder encoder(voy, 1);

    auto nmeas = encoder.as_nmea();
    BOOST_REQUIRE_EQUAL(nmeas.size(), 1);

    std::cout << "encoder OUT: " << nmeas[0].message() << std::endl;

    Decoder decoder(nmeas);
    BOOST_REQUIRE(decoder.complete());
    BOOST_CHECK_EQUAL(decoder.message_id(), 24);
    BOOST_CHECK_EQUAL(decoder.parsed_type(), Decoder::ParsedType::VOYAGE);

    auto voy_out = decoder.as_voyage();
    std::cout << "decoder OUT: " << voy_out.ShortDebugString() << std::endl;

    BOOST_CHECK(voy.SerializeAsString() == voy_out.SerializeAsString());
}
