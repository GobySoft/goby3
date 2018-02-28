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

#include "goby/acomms/dccl/dccl.h"
#include "goby/moos/transitional/dccl_transitional.h"

#include <iostream>
#include <cassert>

using namespace goby;
using goby::acomms::operator<<;
using goby::transitional::operator<<;

int main(int argc, char* argv[])
{
    goby::glog.add_stream(goby::common::logger::DEBUG3, &std::cerr);
    goby::glog.set_name(argv[0]);
    
    std::cout << "loading xml file: " << DCCL_EXAMPLES_DIR "transitional1/test.xml" << std::endl;
    
    // instantiate the parser with a single xml file
    goby::transitional::DCCLTransitionalCodec transitional_dccl;  
    pAcommsHandlerConfig cfg;
    cfg.mutable_transitional_cfg()->add_message_file()->
        set_path(DCCL_EXAMPLES_DIR "/transitional1/test.xml");

    transitional_dccl.convert_to_v2_representation(&cfg);

    std::cout << cfg.DebugString() << std::endl;
    
    std::cout << "all tests passed" << std::endl;
}
