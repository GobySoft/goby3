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

#ifndef GOBY_APPS_ZEROMQ_LIAISON_LIAISON_H
#define GOBY_APPS_ZEROMQ_LIAISON_LIAISON_H

#include <atomic>     // for atomic
#include <functional> // for function
#include <string>     // for string
#include <vector>     // for vector

#include <Wt/WServer> // for WServer

#include "goby/zeromq/application/multi_thread.h" // for MultiThreadApp...
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
    ~Liaison() override
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
