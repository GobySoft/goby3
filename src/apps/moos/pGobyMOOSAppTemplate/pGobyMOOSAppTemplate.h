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

#ifndef GOBY_APPS_MOOS_PGOBYMOOSAPPTEMPLATE_H
#define GOBY_APPS_MOOS_PGOBYMOOSAPPTEMPLATE_H

#include "goby/moos/goby_moos_app.h"

#include "pGobyMOOSAppTemplate_config.pb.h"

namespace goby
{
namespace apps
{
namespace moos
{
class GobyMOOSAppTemplate : public goby::moos::GobyMOOSApp
{
  public:
    static GobyMOOSAppTemplate* get_instance();
    static void delete_instance();

  private:
    GobyMOOSAppTemplate(protobuf::GobyMOOSAppTemplateConfig& cfg);
    ~GobyMOOSAppTemplate();

    void loop(); // from GobyMOOSApp

    void handle_db_time(const CMOOSMsg& msg);

  private:
    protobuf::GobyMOOSAppTemplateConfig& cfg_;
    static GobyMOOSAppTemplate* inst_;
};
} // namespace moos
} // namespace apps
} // namespace goby

#endif
