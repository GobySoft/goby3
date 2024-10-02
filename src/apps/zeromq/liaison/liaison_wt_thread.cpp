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

#include <chrono>      // for time_point
#include <list>        // for operator!=
#include <ostream>     // for basic_ostre...
#include <string>      // for operator+
#include <type_traits> // for __decay_and...
#include <utility>     // for pair, make_...
#include <vector>      // for vector

#include <Wt/WAnchor.h>          // for WAnchor
#include <Wt/WContainerWidget.h> // for WContainerW...
#include <Wt/WFlags.h>           // for Wt
#include <Wt/WGlobal.h>          // for TargetNewWi...
#include <Wt/WImage.h>           // for WImage
#include <Wt/WLink.h>            // for WLink
#include <Wt/WMenu.h>            // for WMenu
#include <Wt/WSignal.h>          // for Signal
#include <Wt/WStackedWidget.h>   // for WStackedWidget
#include <Wt/WString.h>          // for operator<<
#include <Wt/WText.h>            // for WText
#include <Wt/WVBoxLayout.h>
#include <boost/smart_ptr/shared_ptr.hpp> // for shared_ptr
#include <dlfcn.h>                        // for dlsym

#include "goby/time/convert.h"                           // for file_str
#include "goby/util/debug_logger/flex_ostream.h"         // for operator<<
#include "goby/util/debug_logger/flex_ostreambuf.h"      // for DEBUG1, WARN
#include "goby/zeromq/liaison/liaison_container.h"       // for LiaisonCont...
#include "goby/zeromq/protobuf/interprocess_config.pb.h" // for InterProces...

#include "liaison.h" // for Liaison
//#include "liaison_commander.h" // for LiaisonComm...
#include "liaison_home.h" // for LiaisonHome
//#include "liaison_scope.h"     // for LiaisonScope
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
    auto header_div = root()->addNew<WContainerWidget>();
    header_div->setId("header");

    auto header = header_div->addNew<WText>(title_text);
    header->setId("header");

    auto goby_logo = std::make_unique<WImage>("images/gobysoft_logo_dot_org_small.png");

    Wt::WLink goby_link("http://gobysoft.org/#/software/goby");
    goby_link.setTarget(Wt::LinkTarget::NewWindow);
    auto goby_logo_a = header_div->addNew<WAnchor>(goby_link, std::move(goby_logo));
    goby_logo_a->setId("goby_logo");
    goby_logo_a->setStyleClass("no_ul");

    if (app_cfg_.has_upper_right_logo())
    {
        auto goby_lp_image = std::make_unique<WImage>(app_cfg_.upper_right_logo());
        Wt::WLink goby_lp_link(
            app_cfg_.has_upper_right_logo_link() ? app_cfg_.upper_right_logo_link() : "");
        goby_lp_link.setTarget(Wt::LinkTarget::NewWindow);

        auto* goby_lp_image_a = header_div->addNew<WAnchor>(goby_lp_link, std::move(goby_lp_image));
        goby_lp_image_a->setId("lp_logo");
        goby_lp_image_a->setStyleClass("no_ul");
    }

    root()->addNew<Wt::WText>("<hr/>");

    auto menu_div = root()->addNew<WContainerWidget>();
    menu_div->setStyleClass("menu");

    auto contents_div = root()->addNew<WContainerWidget>();
    contents_div->setId("contents");
    auto contents_stack = contents_div->addNew<WStackedWidget>();
    contents_stack->setStyleClass("fill");

    /*
     * Setup the menu
     */
    menu_ = menu_div->addNew<WMenu>(contents_stack);
    menu_->setStyleClass("menu");
    menu_->setInternalPathEnabled();
    menu_->setInternalBasePath("/");

    if (app_cfg_.add_home_tab())
    {
        auto home = std::make_unique<LiaisonHome>();
        add_to_menu(std::move(home));
    }

    // if (app_cfg_.add_scope_tab())
    //     add_to_menu(new LiaisonScope(app_cfg_));
    // if (app_cfg_.add_commander_tab())
    //     add_to_menu(new LiaisonCommander(app_cfg_));

    using liaison_load_func = std::vector<std::unique_ptr<goby::zeromq::LiaisonContainer>> (*)(
        const protobuf::LiaisonConfig& cfg);

    for (auto& plugin_handle : plugin_handles_)
    {
        auto liaison_load_ptr = (liaison_load_func)dlsym(plugin_handle, "goby3_liaison_load");

        if (liaison_load_ptr)
        {
            std::vector<std::unique_ptr<goby::zeromq::LiaisonContainer>> containers =
                (*liaison_load_ptr)(app_cfg_);
            for (auto& container : containers) add_to_menu(std::move(container));
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
        LiaisonContainer* contents = dynamic_cast<LiaisonContainer*>(item->contents());
        if (contents)
        {
            glog.is(DEBUG1) && glog << "Liaison: Cleanup : " << contents->name() << std::endl;
            contents->cleanup();
        }
    }
}

void goby::apps::zeromq::LiaisonWtThread::add_to_menu(std::unique_ptr<LiaisonContainer> container)
{
    Wt::WString name(container->name());
    menu_->addItem(name, std::move(container));
}

void goby::apps::zeromq::LiaisonWtThread::handle_menu_selection(Wt::WMenuItem* item)
{
    LiaisonContainer* contents = dynamic_cast<LiaisonContainer*>(item->contents());
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
            LiaisonContainer* other_contents = dynamic_cast<LiaisonContainer*>(i->contents());
            if (other_contents)
            {
                glog.is(DEBUG1) && glog << "Liaison: Unfocused : " << other_contents->name()
                                        << std::endl;
                other_contents->unfocus();
            }
        }
    }
}
