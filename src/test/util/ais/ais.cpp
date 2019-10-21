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
}
