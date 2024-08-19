// Copyright 2018-2023:
//   GobySoft, LLC (2013-)
//   Community contributors (see AUTHORS file)
// File authors:
//   Toby Schneider <toby@gobysoft.org>
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

#ifndef GOBY_MIDDLEWARE_TERMINATE_TERMINATE_H
#define GOBY_MIDDLEWARE_TERMINATE_TERMINATE_H

#include <sys/types.h>
#include <unistd.h>

#include "goby/util/debug_logger.h"

#include "goby/middleware/marshalling/protobuf.h"
#include "goby/middleware/protobuf/terminate.pb.h"
#include "goby/middleware/terminate/groups.h"

namespace goby
{
namespace middleware
{
namespace terminate
{
/// \brief Checks if the terminate request is for this application, either by target_name or PID
///
/// \param request Request message from goby_terminate
/// \param app_name The current application name (typically cfg().app().name())
/// \return pair: first is a boolean: does this request match us? second is the response if so
inline std::pair<bool, protobuf::TerminateResponse>
check_terminate(const protobuf::TerminateRequest& request, const std::string& app_name)
{
    protobuf::TerminateResponse resp;
    resp.set_target_name(app_name);
    unsigned pid = getpid();
    resp.set_target_pid(pid);

    bool match = false;

    if (request.has_target_name() && request.target_name() == app_name)
    {
        goby::glog.is_debug2() &&
            goby::glog
                << "Received request matching our app name to cleanly quit() from goby_terminate"
                << std::endl;
        match = true;
    }
    else if (request.has_target_pid() && request.target_pid() == pid)
    {
        goby::glog.is_debug2() &&
            goby::glog << "Received request matching our PID to cleanly quit() from goby_terminate"
                       << std::endl;
        match = true;
    }
    return std::make_pair(match, resp);
}

template <typename Derived> class Application
{
  protected:
    void subscribe_terminate(bool do_quit = true)
    {
        // handle goby_terminate request
        static_cast<Derived*>(this)
            ->interprocess()
            .template subscribe<goby::middleware::groups::terminate_request,
                                goby::middleware::protobuf::TerminateRequest>(
                [this, do_quit](const goby::middleware::protobuf::TerminateRequest& request)
                {
                    bool match = false;
                    goby::middleware::protobuf::TerminateResponse resp;
                    std::tie(match, resp) = goby::middleware::terminate::check_terminate(
                        request, static_cast<Derived*>(this)->app_cfg().app().name());
                    if (match)
                    {
                        static_cast<Derived*>(this)
                            ->interprocess()
                            .template publish<goby::middleware::groups::terminate_response>(resp);
                        if (do_quit)
                            static_cast<Derived*>(this)->quit();
                    }
                });
    }
};

} // namespace terminate
} // namespace middleware
} // namespace goby

#endif
