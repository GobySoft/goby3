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

#include "goby/util/debug_logger.h"
#include <boost/asio/deadline_timer.hpp> // for deadline_timer
#include <fstream>

using goby::util::Colors;
using namespace goby::util::tcolor; // red, blue, etc.
using namespace goby::util::logger; // VERBOSE, DEBUG, WARN, etc.
using goby::glog;

void output();

int main(int argc, char* argv[])
{
    if (argc < 2)
    {
        std::cout << "usage: flexostream quiet|warn|verbose|debug|gui [file.txt]" << std::endl;
        return 1;
    }

    // write our name with each log entry
    goby::glog.set_name(argv[0]);

    // set colors and descriptions for the groups
    goby::glog.add_group("a", Colors::green, "group a");
    goby::glog.add_group("b", Colors::magenta, "group b");
    goby::glog.add_group("c", Colors::blue, "group c");

    std::string verbosity = argv[1];

    std::ofstream fout;
    if (argc == 3)
    {
        fout.open(argv[2]);
        if (fout.is_open())
        {
            goby::glog.add_stream(goby::util::logger::DEBUG1, &fout);
        }
        else
        {
            std::cerr << "Could not open " << argv[2] << " for writing!" << std::endl;
            return 1;
        }
    }

    if (verbosity == "quiet")
    {
        std::cout << "--- testing quiet ---" << std::endl;
        // add a stream with the quiet setting
        goby::glog.add_stream(goby::util::logger::QUIET, &std::cout);
        output();
    }
    else if (verbosity == "warn")
    {
        std::cout << "--- testing warn ---" << std::endl;
        // add a stream with the quiet setting
        goby::glog.add_stream(goby::util::logger::WARN, &std::cout);
        output();
    }
    else if (verbosity == "verbose")
    {
        std::cout << "--- testing verbose ---" << std::endl;
        // add a stream with the quiet setting
        goby::glog.add_stream(goby::util::logger::VERBOSE, &std::cout);
        output();
    }
    else if (verbosity == "debug")
    {
        std::cout << "--- testing debug 1---" << std::endl;
        // add a stream with the quiet setting
        goby::glog.add_stream(goby::util::logger::DEBUG1, &std::cout);
        output();
    }
    else if (verbosity == "gui")
    {
        std::cout << "--- testing gui ---" << std::endl;
        // add a stream with the quiet setting
        goby::glog.add_stream(goby::util::logger::VERBOSE, &std::cout);
        goby::glog.enable_gui();
        output();

        const int CLOSE_TIME = 60;
        goby::glog << warn << "closing in " << CLOSE_TIME << " seconds!" << std::endl;

        // `sleep` is not thread safe, so we use a boost::asio::deadline_timer
        boost::asio::io_service io_service;
        boost::asio::deadline_timer timer(io_service);
        timer.expires_from_now(boost::posix_time::seconds(CLOSE_TIME));
        timer.wait();
    }
    else
    {
        std::cout << "invalid verbosity setting: " << verbosity << std::endl;
        return 1;
    }

    if (fout.is_open())
        fout.close();
    return 0;
}

void output()
{
    glog.is(WARN) && glog << "this is warning text" << std::endl;
    glog.is(VERBOSE) && glog << "this is normal text" << std::endl;
    glog.is(VERBOSE) && glog << lt_blue << "this is light blue text (in color terminals)" << nocolor
                             << std::endl;
    glog.is(DEBUG1) && glog << "this is debug text" << std::endl;

    glog.is(VERBOSE) && glog << group("a") << "this text is related to a" << std::endl;
    glog.is(VERBOSE) && glog << group("b") << "this text is related to b" << std::endl;
    glog.is(VERBOSE) && glog << group("c") << warn << "this warning is related to c" << std::endl;
}
