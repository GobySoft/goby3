// Copyright 2019-2020:
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

#ifndef LIAISONHOME20110609H
#define LIAISONHOME20110609H

#include <Wt/WBorder>
#include <Wt/WColor>
#include <Wt/WCssDecorationStyle>
#include <Wt/WText>
#include <Wt/WVBoxLayout>

#include "liaison.h"

namespace goby
{
namespace apps
{
namespace zeromq
{
class LiaisonHome : public goby::zeromq::LiaisonContainer
{
  public:
    LiaisonHome();

  private:
    Wt::WVBoxLayout* main_layout_;
};
} // namespace zeromq
} // namespace goby
}

#endif
