// Copyright 2026:
//   GobySoft, LLC (2013-)
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

// Not run: compiled so that goby_clang_tool -gen can read it (see run.sh). Each call uses a
// DynamicGroup made in a different way, as applications do.

#include <memory>

#include "goby/middleware/marshalling/protobuf.h"
#include "goby/test/unit/middleware/clang_tool_gen/dynamic_groups.pb.h"
#include "goby/udpm/application/single_thread.h"

using goby::middleware::DynamicGroup;
using goby::test::middleware::protobuf::DynamicGroupsConfig;
using goby::test::middleware::protobuf::DynamicGroupsStatus;

namespace groups
{
constexpr goby::middleware::Group status{"dynamic_test::status"};
constexpr goby::middleware::Group command{"dynamic_test::command"};
constexpr goby::middleware::Group plain{"dynamic_test::plain"};
constexpr std::uint32_t fleet_base{100};
} // namespace groups

GOBY_DEFINE_INTERVEHICLE_GROUP(groups, broadcast, goby::middleware::Group::broadcast_group)
GOBY_DEFINE_INTERVEHICLE_GROUP(groups, fleet, groups::fleet_base + 5)

// a helper that builds the group, as jaiabot's hub_command_group(bot_id) does
DynamicGroup command_group(std::uint32_t id) { return DynamicGroup(groups::command, id + 100); }

// set in the constructor and used elsewhere, as jaiabot's hub_command_this_bot is
std::unique_ptr<DynamicGroup> command_this_vehicle;

class DynamicGroups : public goby::udpm::SingleThreadApplication<DynamicGroupsConfig>
{
  public:
    DynamicGroups()
        : status_group_(groups::status, cfg().modem_id()), runtime_group_(cfg().runtime_group())
    {
        command_this_vehicle.reset(new DynamicGroup(command_group(cfg().modem_id())));

        DynamicGroupsStatus status;
        // a string literal
        interprocess().publish_dynamic(status, DynamicGroup("dynamic_test::literal"));
        // a constexpr Group and a constant numeric
        interprocess().publish_dynamic(status, DynamicGroup(groups::status, 3));
        // a member with its numeric from the configuration
        interprocess().publish_dynamic(status, status_group_);
        // a helper function
        interprocess().publish_dynamic(status, command_group(cfg().modem_id()));
        // a member with its string from the configuration: goby_clang_tool cannot tell the group
        interprocess().publish_dynamic(status, runtime_group_);

        // static groups whose numerics are constant expressions, not literals
        interprocess().publish<groups::broadcast>(status);
        interprocess().publish<groups::fleet>(status);
        // and one without a numeric
        interprocess().publish<groups::plain>(status);

        // a smart pointer set above
        interprocess()
            .subscribe_dynamic<DynamicGroupsStatus, goby::middleware::MarshallingScheme::PROTOBUF,
                               goby::middleware::Necessity::REQUIRED>(
                [](const DynamicGroupsStatus&) {}, *command_this_vehicle);
        // a local variable
        DynamicGroup local("dynamic_test::local");
        interprocess().subscribe_dynamic<DynamicGroupsStatus>([](const DynamicGroupsStatus&) {},
                                                              local);
    }

  private:
    void loop() override {}

    DynamicGroup status_group_;
    DynamicGroup runtime_group_;
};

int main(int argc, char* argv[]) { return goby::run<DynamicGroups>(argc, argv); }
