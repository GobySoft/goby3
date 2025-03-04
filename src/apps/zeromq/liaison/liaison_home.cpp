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

#include "liaison_home.h"

#include <Wt/WBreak.h>           // for WBreak
#include <Wt/WContainerWidget.h> // for WContainerWidget
#include <Wt/WLength.h>          // for Wt
#include <Wt/WText.h>            // for WText
#include <Wt/WVBoxLayout.h>      // for WVBoxLayout

goby::apps::zeromq::LiaisonHome::LiaisonHome()
{
    auto main_layout = std::make_unique<Wt::WVBoxLayout>();

    auto top_text = std::make_unique<Wt::WContainerWidget>();
    top_text->addNew<Wt::WText>("Welcome to Goby Liaison: an extensible tool for commanding and "
                                "comprehending this Goby platform.");
    top_text->addNew<Wt::WBreak>();

    top_text->addNew<Wt::WText>("<i>liaison (n): one that establishes and maintains "
                                "communication for mutual understanding and cooperation</i>");
    main_layout->addWidget(std::move(top_text));

    this->setLayout(std::move(main_layout));
    set_name("Home");
}
