// Copyright 2012-2020:
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

#ifndef LIAISONWTTHREAD20110609H
#define LIAISONWTTHREAD20110609H

#include <Wt/WApplication>
#include <Wt/WContainerWidget>
#include <Wt/WEnvironment>
#include <Wt/WMenu>
#include <Wt/WServer>
#include <Wt/WString>
#include <Wt/WTimer>

#include "goby/zeromq/protobuf/liaison_config.pb.h"
#include "liaison.h"

namespace goby
{
namespace apps
{
namespace zeromq
{
class LiaisonWtThread : public Wt::WApplication
{
  public:
    LiaisonWtThread(const Wt::WEnvironment& env, protobuf::LiaisonConfig app_cfg);
    ~LiaisonWtThread();

    LiaisonWtThread(const LiaisonWtThread&) = delete;
    LiaisonWtThread& operator=(const LiaisonWtThread&) = delete;

    void add_to_menu(Wt::WMenu* menu, goby::zeromq::LiaisonContainer* container);
    void handle_menu_selection(Wt::WMenuItem* item);

  private:
    Wt::WMenu* menu_;
    Wt::WStackedWidget* contents_stack_;
    std::map<Wt::WMenuItem*, goby::zeromq::LiaisonContainer*> menu_contents_;
    protobuf::LiaisonConfig app_cfg_;
};
} // namespace zeromq
} // namespace goby
}

#endif
