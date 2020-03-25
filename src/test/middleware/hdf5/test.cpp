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

#include <cassert>
#include <cstdio>

#include "goby/util/debug_logger.h"

using goby::glog;

int main(int argc, char* argv[])
{
    goby::glog.add_stream(goby::util::logger::DEBUG3, &std::cerr);
    goby::glog.set_name(argv[0]);

    std::string sys_cmd("LD_LIBRARY_PATH=" GOBY_LIB_DIR
                        ":$LD_LIBRARY_PATH GOBY_HDF5_PLUGIN=libgoby_hdf5test.so goby_hdf5 "
                        "--output_file /tmp/test.h5");
    std::cout << "Running: [" << sys_cmd << "]" << std::endl;
    int rc = system(sys_cmd.c_str());

    if (rc == 0)
    {
        std::cout << "All tests passed." << std::endl;
    }

    return rc;
}
