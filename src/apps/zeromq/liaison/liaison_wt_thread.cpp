// Copyright 2012-2021:
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

#include <chrono>      // for time_point
#include <list>        // for operator!=
#include <ostream>     // for basic_ostre...
#include <string>      // for operator+
#include <type_traits> // for __decay_and...
#include <utility>     // for pair, make_...
#include <vector>      // for vector

#include <Wt/WAnchor>                     // for WAnchor
#include <Wt/WContainerWidget>            // for WContainerW...
#include <Wt/WFlags>                      // for Wt
#include <Wt/WGlobal>                     // for TargetNewWi...
#include <Wt/WImage>                      // for WImage
#include <Wt/WLink>                       // for WLink
#include <Wt/WMenu>                       // for WMenu
#include <Wt/WSignal>                     // for Signal
#include <Wt/WStackedWidget>              // for WStackedWidget
#include <Wt/WString>                     // for operator<<
#include <Wt/WText>                       // for WText
#include <boost/smart_ptr/shared_ptr.hpp> // for shared_ptr
#include <dlfcn.h>                        // for dlsym

#include "goby/time/convert.h"                           // for file_str
#include "goby/util/debug_logger/flex_ostream.h"         // for operator<<
#include "goby/util/debug_logger/flex_ostreambuf.h"      // for DEBUG1, WARN
#include "goby/zeromq/liaison/liaison_container.h"       // for LiaisonCont...
#include "goby/zeromq/protobuf/interprocess_config.pb.h" // for InterProces...

#include "liaison.h"           // for Liaison
#include "liaison_commander.h" // for LiaisonComm...
#include "liaison_home.h"      // for LiaisonHome
#include "liaison_scope.h"     // for LiaisonScope
#include "liaison_wt_thread.h"

namespace Wt
{
class WMenuItem;
} // namespace Wt

using goby::glog;
using namespace Wt;
using namespace goby::util::logger;
using goby::zeromq::LiaisonContainer;

std::vector<void*> goby::apps::zeromq::LiaisonWtThread::plugin_handles_;

goby::apps::zeromq::LiaisonWtThread::LiaisonWtThread(const Wt::WEnvironment& env,
                                                     protobuf::LiaisonConfig app_cfg)
    : Wt::WApplication(env), app_cfg_(std::move(app_cfg))
{
    Wt::WString title_text("goby liaison: " + app_cfg_.interprocess().platform());
    setTitle(title_text);

    useStyleSheet(std::string("css/fonts.css?" + time::file_str()));
    useStyleSheet(std::string("css/liaison.css?" + time::file_str()));
    setCssTheme("default");

    root()->setId("main");

    /*
     * Set up the title
     */
    auto* header_div = new WContainerWidget(root());
    header_div->setId("header");

    auto* header = new WText(title_text, header_div);
    header->setId("header");

    auto* goby_logo = new WImage("images/gobysoft_logo_dot_org_small.png");
    auto* goby_logo_a = new WAnchor("http://gobysoft.org/#/software/goby", goby_logo, header_div);
    goby_logo_a->setId("goby_logo");
    goby_logo_a->setStyleClass("no_ul");
    goby_logo_a->setTarget(TargetNewWindow);

    if (app_cfg_.has_upper_right_logo())
    {
        auto* goby_lp_image = new WImage(app_cfg_.upper_right_logo());
        auto* goby_lp_image_a = new WAnchor(
            app_cfg_.has_upper_right_logo_link() ? app_cfg_.upper_right_logo_link() : "",
            goby_lp_image, header_div);
        goby_lp_image_a->setId("lp_logo");
        goby_lp_image_a->setStyleClass("no_ul");
        goby_lp_image_a->setTarget(TargetNewWindow);
    }

    new WText("<hr/>", root());

    auto* menu_div = new WContainerWidget(root());
    menu_div->setStyleClass("menu");

    auto* contents_div = new WContainerWidget(root());
    contents_div->setId("contents");
    contents_stack_ = new WStackedWidget(contents_div);
    contents_stack_->setStyleClass("fill");

    /*
     * Setup the menu
     */
    menu_ = new WMenu(contents_stack_, Vertical, menu_div);
    menu_->setRenderAsList(true);
    menu_->setStyleClass("menu");
    menu_->setInternalPathEnabled();
    menu_->setInternalBasePath("/");

    if (app_cfg_.add_home_tab())
        add_to_menu(menu_, new LiaisonHome);
    if (app_cfg_.add_scope_tab())
        add_to_menu(menu_, new LiaisonScope(app_cfg_));
    if (app_cfg_.add_commander_tab())
        add_to_menu(menu_, new LiaisonCommander(app_cfg_));

    using liaison_load_func =
        std::vector<goby::zeromq::LiaisonContainer*> (*)(const protobuf::LiaisonConfig& cfg);

    for (auto& plugin_handle : plugin_handles_)
    {
        auto liaison_load_ptr = (liaison_load_func)dlsym(plugin_handle, "goby3_liaison_load");

        if (liaison_load_ptr)
        {
            std::vector<goby::zeromq::LiaisonContainer*> containers = (*liaison_load_ptr)(app_cfg_);
            for (auto& container : containers) add_to_menu(menu_, container);
        }
        else
        {
            glog.is(WARN) &&
                glog << "Liaison: Cannot find function 'goby3_liaison_load' in plugin library."
                     << std::endl;
        }
    }

    menu_->itemSelected().connect(this, &LiaisonWtThread::handle_menu_selection);

    handle_menu_selection(menu_->currentItem());
}

goby::apps::zeromq::LiaisonWtThread::~LiaisonWtThread()
{
    // run on all children
    const std::vector<WMenuItem*>& items = menu_->items();
    for (auto item : items)
    {
        LiaisonContainer* contents = menu_contents_[item];
        if (contents)
        {
            glog.is(DEBUG1) && glog << "Liaison: Cleanup : " << contents->name() << std::endl;
            contents->cleanup();
        }
    }
}

void goby::apps::zeromq::LiaisonWtThread::add_to_menu(WMenu* menu, LiaisonContainer* container)
{
    Wt::WMenuItem* new_item = menu->addItem(container->name(), container);
    menu_contents_.insert(std::make_pair(new_item, container));
}

void goby::apps::zeromq::LiaisonWtThread::handle_menu_selection(Wt::WMenuItem* item)
{
    LiaisonContainer* contents = menu_contents_[item];
    if (contents)
    {
        glog.is(DEBUG1) && glog << "Liaison: Focused : " << contents->name() << std::endl;

        contents->focus();
    }
    else
    {
        glog.is(WARN) && glog << "Liaison: Invalid menu item!" << std::endl;
    }

    // unfocus all others
    const std::vector<WMenuItem*>& items = menu_->items();
    for (auto i : items)
    {
        if (i != item)
        {
            LiaisonContainer* other_contents = menu_contents_[i];
            if (other_contents)
            {
                glog.is(DEBUG1) && glog << "Liaison: Unfocused : " << other_contents->name()
                                        << std::endl;
                other_contents->unfocus();
            }
        }
    }
}
