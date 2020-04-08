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

#include <Wt/WApplication>
#include <Wt/WContainerWidget>
#include <Wt/WEnvironment>
#include <Wt/WMenu>
#include <Wt/WServer>
#include <Wt/WString>
#include <Wt/WTimer>

#include "goby/middleware/marshalling/protobuf.h"
#include "goby/zeromq/application/multi_thread.h"
#include "goby/zeromq/liaison/liaison_container.h"
#include "goby/zeromq/protobuf/liaison_config.pb.h"

namespace goby
{
namespace apps
{
namespace zeromq
{
class Liaison : public goby::zeromq::MultiThreadApplication<protobuf::LiaisonConfig>
{
  public:
    Liaison();
    ~Liaison()
    {
        terminating_ = true;
        wt_server_.stop();
    }

    void loop() override;

    static std::vector<void*> plugin_handles_;

  private:
    void load_proto_file(const std::string& path);

    friend class LiaisonWtThread;

  private:
    Wt::WServer wt_server_;
    std::atomic<bool> terminating_{false};
    std::function<void()> expire_sessions_;
};

} // namespace zeromq
} // namespace apps
} // namespace goby

#endif
