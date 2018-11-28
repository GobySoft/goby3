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

#include "goby/middleware/single-thread-application.h"

#include "goby/middleware/protobuf/terminate_config.pb.h"
#include "goby/middleware/protobuf/terminate.pb.h"
#include "goby/middleware/terminate/groups.h"

using goby::glog;

namespace goby
{
    class Terminate : public goby::SingleThreadApplication<goby::protobuf::TerminateConfig>
    {
    public:
        Terminate() :
            goby::SingleThreadApplication<goby::protobuf::TerminateConfig>(10*boost::units::si::hertz)
            {
                if(cfg().target_name_size() == 0 && cfg().target_pid_size() == 0)
                    glog.is_die() && glog << "Error, must specify at least one --target_name or --target_pid" << std::endl;
                
                interprocess().subscribe<
                    groups::terminate_response, protobuf::TerminateResponse>(
                        [this](const protobuf::TerminateResponse& response)
                        {
                            std::string target_name = response.has_target_name() ? response.target_name() : pid_to_string(response.target_pid());
                            
                            auto it = waiting_for_targets_.find(target_name);
                            if(it != waiting_for_targets_.end())
                            {
                                glog.is_debug2() && glog << "Received terminate response from our target: " << target_name << std::endl;
                            
                                waiting_for_targets_.erase(it);
                            
                                if(waiting_for_targets_.empty())
                                {
                                    glog.is_debug2() && glog << "All targets responded; quitting cleanly." << std::endl;
                                    quit();
                                }
                            
                            }
                            else
                            {
                                glog.is_debug2() && glog << "Received terminate response from a target that we are not tracking: " << response.ShortDebugString() << std::endl;
                            }
                        });

                for(const auto& target_name : cfg().target_name())
                {
                    protobuf::TerminateRequest req;
                    req.set_target_name(target_name);
                    waiting_for_targets_.insert(target_name);
                    glog.is_debug2() && glog << "Sending terminate request: " << req.ShortDebugString() << std::endl;
                    interprocess().publish<groups::terminate_request>(req);
                }

                for(const auto& target_pid : cfg().target_pid())
                {
                    protobuf::TerminateRequest req;
                    req.set_target_pid(target_pid);
                    waiting_for_targets_.insert(pid_to_string(target_pid));
                    glog.is_debug2() && glog << "Sending terminate request: " << req.ShortDebugString() << std::endl;
                    interprocess().publish<groups::terminate_request>(req);
                }

            }
        
        ~Terminate() { }

    private:
        void loop() override
            {
                decltype(start_time_) now(goby::time::now()),
                    timeout(cfg().response_timeout_with_units());
                if(now > start_time_ + timeout)
                {
                    if(glog.is_warn())
                    {
                        glog << "Timeout waiting for response from targets: ";
                        for(const auto& target_name : waiting_for_targets_)
                            glog << target_name << ", ";
                        glog << std::endl;
                    }
                    quit(EXIT_FAILURE);
                }
            }

        std::string pid_to_string(unsigned pid) 
            { return "PID:" + std::to_string(pid); }
        
                
    private:
        goby::time::MicroTime start_time_{ goby::time::now() };
        std::set<std::string> waiting_for_targets_;
    };
}

int main(int argc, char* argv[])
{ return goby::run<goby::Terminate>(argc, argv); }
