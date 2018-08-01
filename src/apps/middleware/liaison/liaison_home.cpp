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

#include "liaison_home.h"

using namespace Wt;


goby::common::LiaisonHome::LiaisonHome(goby::SimpleThread<protobuf::LiaisonConfig>* goby_thread, Wt::WContainerWidget* parent)
    : LiaisonContainer(parent),
      main_layout_(new Wt::WVBoxLayout(this))
{
    Wt::WContainerWidget* top_text = new Wt::WContainerWidget(this);
    main_layout_->addWidget(top_text);
    
    top_text->addWidget(new WText("Welcome to Goby Liaison: an extensible tool for commanding and comprehending this Goby platform."));
    top_text->addWidget(new WBreak());
    top_text->addWidget(new WText("<i>liaison (n): one that establishes and maintains communication for mutual understanding and cooperation</i>"));

    set_name("Home");
}

