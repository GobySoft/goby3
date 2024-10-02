// Copyright 2012-2022:
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

#ifndef GOBY_APPS_ZEROMQ_LIAISON_LIAISON_WT_THREAD_H
#define GOBY_APPS_ZEROMQ_LIAISON_LIAISON_WT_THREAD_H

#include <map> // for map

#include <Wt/WApplication.h> // for WApplication
#include <Wt/WEnvironment.h> // for WEnvironment

#include "goby/zeromq/protobuf/liaison_config.pb.h" // for LiaisonConfig

namespace Wt
{
class WMenu;
class WMenuItem;
class WStackedWidget;
} // namespace Wt

namespace goby
{
namespace zeromq
{
class LiaisonContainer;
} // namespace zeromq

namespace apps
{
namespace zeromq
{
class LiaisonWtThread : public Wt::WApplication
{
  public:
    LiaisonWtThread(const Wt::WEnvironment& env, protobuf::LiaisonConfig app_cfg);
    ~LiaisonWtThread() override;

    LiaisonWtThread(const LiaisonWtThread&) = delete;
    LiaisonWtThread& operator=(const LiaisonWtThread&) = delete;

    void add_to_menu(std::unique_ptr<goby::zeromq::LiaisonContainer> container);
    void handle_menu_selection(Wt::WMenuItem* item);

    static std::vector<void*> plugin_handles_;

  private:
    Wt::WMenu* menu_;
    protobuf::LiaisonConfig app_cfg_;
};
} // namespace zeromq
} // namespace apps
} // namespace goby

#endif
