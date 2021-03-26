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

#ifndef GOBY_APPS_ZEROMQ_FRONTSEAT_INTERFACE_FRONTSEAT_INTERFACE_H
#define GOBY_APPS_ZEROMQ_FRONTSEAT_INTERFACE_FRONTSEAT_INTERFACE_H

#include <memory> // for unique_ptr

#include "goby/middleware/frontseat/interface.h"                // for InterfaceBase
#include "goby/zeromq/application/multi_thread.h"               // for MultiThreadApp...
#include "goby/zeromq/protobuf/frontseat_interface_config.pb.h" // for Fron...

namespace goby
{
namespace apps
{
namespace zeromq
{
class FrontSeatInterface
    : public goby::zeromq::MultiThreadApplication<protobuf::FrontSeatInterfaceConfig>
{
  public:
    FrontSeatInterface();
    ~FrontSeatInterface() override = default;

    static void* driver_library_handle_;

  private:
    void loop() override;
    void setup_subscriptions();
    void launch_helm_interface();

    enum
    {
        STATUS_TIMER = 0
    };

  private:
    std::unique_ptr<goby::middleware::frontseat::InterfaceBase> frontseat_;
};

} // namespace zeromq
} // namespace apps
} // namespace goby

#endif
