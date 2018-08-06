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

#ifndef LIAISON20110609H
#define LIAISON20110609H

#include <google/protobuf/descriptor.h>
#include <mutex>

#include <Wt/WEnvironment>
#include <Wt/WApplication>
#include <Wt/WString>
#include <Wt/WMenu>
#include <Wt/WServer>
#include <Wt/WContainerWidget>
#include <Wt/WTimer>

#include "goby/middleware/liaison/liaison_container.h"
#include "goby/middleware/protobuf/liaison_config.pb.h"

#include "goby/middleware/multi-thread-application.h"

namespace goby
{
    namespace common
    {

        class Liaison : public MultiThreadApplication<protobuf::LiaisonConfig>
        {
          public:
            Liaison();
            ~Liaison() { }
            
            void loop() override;

            static std::vector<void *> plugin_handles_;

            
          private:
            void load_proto_file(const std::string& path);
            
            friend class LiaisonWtThread;
          private:
            Wt::WServer wt_server_;
            bool terminating_ {false};
            std::function<void()> expire_sessions_;
        };

    }
}



#endif
