// Copyright 2011-2021:
//   GobySoft, LLC (2013-)
//   Massachusetts Institute of Technology (2007-2014)
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

#define BOOST_TEST_MODULE nmea_test
#include <boost/test/included/unit_test.hpp>

#include "goby/util/binary.h"
#include "goby/util/linebasedcomms.h"

bool close_enough(double a, double b, int precision)
{
    return std::abs(a - b) < std::pow(10, -precision);
}

BOOST_AUTO_TEST_CASE(fooba)
{
    goby::util::NMEASentence nmea;
    nmea.push_back("$FOOBA");
    nmea.push_back(1);
    nmea.push_back(2);
    nmea.push_back(3);
    nmea.push_back("");
    std::cout << nmea.message() << std::endl;
    BOOST_CHECK_EQUAL(nmea.message(), "$FOOBA,1,2,3,*75");
}

BOOST_AUTO_TEST_CASE(cctxd_gen)
{
    goby::util::NMEASentence nmea;
    nmea.push_back("$CCTXD");
    nmea.push_back(2);
    nmea.push_back("1,1");
    nmea.push_back(goby::util::hex_encode(""));
    std::cout << nmea.message() << std::endl;
    BOOST_CHECK_EQUAL(nmea.message(), "$CCTXD,2,1,1,*7A");
}

BOOST_AUTO_TEST_CASE(cctxd_parse)
{
    goby::util::NMEASentence nmea("$CCTXD,2,1,1*56");
    BOOST_CHECK_EQUAL(nmea.as<int>(3), 1);
    BOOST_CHECK_EQUAL(nmea.at(3), "1");
}

BOOST_AUTO_TEST_CASE(yxxdr)
{
    goby::util::NMEASentence nmea("$YXXDR,A,0.3,D,PTCH,A,13.3,D,ROLL*6f ");
    std::cout << nmea.message() << std::endl;
    BOOST_CHECK_EQUAL(nmea.at(8), "ROLL");
}

BOOST_AUTO_TEST_CASE(aivdo)
{
    goby::util::NMEASentence nmea("!AIVDO,1,1,,,B0000003wk?8mP=18D3Q3wwUkP06,0*7B");
    std::cout << nmea.message() << std::endl;
    BOOST_CHECK_EQUAL(nmea.as<int>(1), 1);
    BOOST_CHECK_EQUAL(nmea.as<int>(2), 1);
}

BOOST_AUTO_TEST_CASE(gps_rmc)
{
    std::string orig = "$GPRMC,225446,A,4916.45,N,12311.12,W,000.5,054.7,191194,020.3,E*68";
    goby::util::NMEASentence nmea_in(orig);
    goby::util::gps::RMC rmc(nmea_in);
    BOOST_CHECK_EQUAL(goby::time::convert<boost::posix_time::ptime>(*rmc.time),
                      boost::posix_time::ptime({1994, 11, 19}, {22, 54, 46}));
    BOOST_CHECK(close_enough(rmc.latitude->value(), 49.274167, 6));
    BOOST_CHECK(close_enough(rmc.longitude->value(), -123.1853333, 6));
    BOOST_CHECK(close_enough(rmc.speed_over_ground->value(), 0.257222, 5));
    BOOST_CHECK(close_enough(rmc.course_over_ground->value(), 54.7, 1));
    BOOST_CHECK(close_enough(rmc.magnetic_variation->value(), 20.3, 1));
    BOOST_CHECK_EQUAL(*rmc.status, goby::util::gps::RMC::DataValid);

    auto nmea = rmc.serialize();

    std::cout << orig << std::endl;
    std::cout << nmea.message() << std::endl;

    // reparse output and check equality
    goby::util::gps::RMC rmc2(nmea);
    BOOST_CHECK_EQUAL(rmc, rmc2);
    std::cout << rmc2.serialize().message() << std::endl;
}

BOOST_AUTO_TEST_CASE(gps_hdt)
{
    std::string orig = "$GPHDT,75.5664,T*36";
    goby::util::NMEASentence nmea_in(orig);
    goby::util::gps::HDT hdt(nmea_in);
    BOOST_CHECK(close_enough(hdt.true_heading->value(), 75.5664, 4));

    auto nmea = hdt.serialize();

    std::cout << orig << std::endl;
    std::cout << nmea.message() << std::endl;

    // reparse output and check equality
    goby::util::gps::HDT hdt2(nmea);
    BOOST_CHECK_EQUAL(hdt, hdt2);
    std::cout << hdt2.serialize().message() << std::endl;
}

BOOST_AUTO_TEST_CASE(gps_wpl)
{
    std::string orig = "$ECWPL,4135.868,N,07043.697,W,*45";
    goby::util::NMEASentence nmea_in(orig);
    goby::util::gps::WPL wpl(nmea_in);
    BOOST_CHECK(close_enough(wpl.latitude->value(), 41.5978, 4));
    BOOST_CHECK(close_enough(wpl.longitude->value(), -70.7282, 4));

    auto nmea = wpl.serialize();

    std::cout << orig << std::endl;
    std::cout << nmea.message() << std::endl;

    // reparse output and check equality
    goby::util::gps::WPL wpl2(nmea);
    BOOST_CHECK_EQUAL(wpl, wpl2);
    std::cout << wpl2.serialize().message() << std::endl;
}

//$ECRTE,3,1,c,test,001,002*31
//$ECRTE,3,2,c,test,003,004*36
//$ECRTE,3,3,c,test,005*29
BOOST_AUTO_TEST_CASE(gps_rte1)
{
    std::string orig = "$ECRTE,3,1,c,test,001,002*31";
    goby::util::NMEASentence nmea_in(orig);
    goby::util::gps::RTE rte(nmea_in);
    BOOST_CHECK_EQUAL(*rte.total_number_sentences, 3);
    BOOST_CHECK_EQUAL(*rte.current_sentence_index, 1);
    BOOST_CHECK_EQUAL(rte.type, goby::util::gps::RTE::ROUTE_TYPE__COMPLETE);
    BOOST_CHECK_EQUAL(*rte.name, "test");
    BOOST_CHECK_EQUAL(rte.waypoint_names.size(), 2);
    BOOST_CHECK_EQUAL(rte.waypoint_names.at(0), "001");
    BOOST_CHECK_EQUAL(rte.waypoint_names.at(1), "002");

    auto nmea = rte.serialize();

    std::cout << orig << std::endl;
    std::cout << nmea.message() << std::endl;

    // reparse output and check equality
    goby::util::gps::RTE rte2(nmea);
    BOOST_CHECK_EQUAL(rte, rte2);
    std::cout << rte2.serialize().message() << std::endl;
}
// rte2 is same as rte1 really, so skip
BOOST_AUTO_TEST_CASE(gps_rte3)
{
    std::string orig = "$ECRTE,3,3,c,test,005*29";
    goby::util::NMEASentence nmea_in(orig);
    goby::util::gps::RTE rte(nmea_in);
    BOOST_CHECK_EQUAL(*rte.total_number_sentences, 3);
    BOOST_CHECK_EQUAL(*rte.current_sentence_index, 3);
    BOOST_CHECK_EQUAL(rte.type, goby::util::gps::RTE::ROUTE_TYPE__COMPLETE);
    BOOST_CHECK_EQUAL(*rte.name, "test");
    BOOST_CHECK_EQUAL(rte.waypoint_names.size(), 1);
    BOOST_CHECK_EQUAL(rte.waypoint_names.at(0), "005");

    auto nmea = rte.serialize();

    std::cout << orig << std::endl;
    std::cout << nmea.message() << std::endl;

    // reparse output and check equality
    goby::util::gps::RTE rte2(nmea);
    BOOST_CHECK_EQUAL(rte, rte2);
    std::cout << rte2.serialize().message() << std::endl;
}
