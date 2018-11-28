// Copyright 2009-2018 Toby Schneider (http://gobysoft.org/index.wt/people/toby)
//                     GobySoft, LLC (2013-)
//                     Massachusetts Institute of Technology (2007-2014)
//                     Community contributors (see AUTHORS file)
//
//
// This file is part of the Goby Underwater Autonomy Project Libraries
// ("The Goby Libraries").
//
// The Goby Libraries are free software: you can redistribute them and/or modify
// them under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 2.1 of the License, or
// (at your option) any later version.
//
// The Goby Libraries are distributed in the hope that they will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with Goby.  If not, see <http://www.gnu.org/licenses/>.

#ifndef TERMINATE_20181128H
#define TERMINATE_20181128H

#include <sys/types.h>
#include <unistd.h>

#include "goby/common/logger.h"

#include "goby/middleware/protobuf/terminate.pb.h"
#include "goby/middleware/terminate/groups.h"

namespace goby
{
    namespace terminate
    {
        /// return pair: first is a boolean: does this request match us? second is the response if so
        inline std::pair<bool, protobuf::TerminateResponse> check_terminate(const protobuf::TerminateRequest& request, const std::string& app_name)
        {
            protobuf::TerminateResponse resp;
            bool match = false;
            if(request.has_target_name() && request.target_name() == app_name)
            {
                goby::glog.is_debug2() && goby::glog << "Received request matching our app name to cleanly quit() from goby_terminate" << std::endl;
                resp.set_target_name(app_name);
                match = true;
            }
            else if(request.has_target_pid() && request.target_pid() == getpid())
            {
                goby::glog.is_debug2() && goby::glog << "Received request matching our PID to cleanly quit() from goby_terminate" << std::endl;
                resp.set_target_pid(request.target_pid());
                match = true;
            }
            return std::make_pair(match, resp);
        }
    }
}


#endif
