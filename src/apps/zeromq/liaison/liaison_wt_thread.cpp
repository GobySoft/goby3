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

#include <dlfcn.h>

#include <Wt/WAnchor>
#include <Wt/WHBoxLayout>
#include <Wt/WImage>
#include <Wt/WStackedWidget>
#include <Wt/WText>
#include <Wt/WVBoxLayout>

#include "dccl/dynamic_protobuf_manager.h"
#include "goby/time.h"

#include "liaison_commander.h"
#include "liaison_home.h"
#include "liaison_scope.h"
#include "liaison_wt_thread.h"

using goby::glog;
using namespace Wt;
using namespace goby::util::logger;
using goby::zeromq::LiaisonContainer;

goby::apps::zeromq::LiaisonWtThread::LiaisonWtThread(const Wt::WEnvironment& env,
                                               protobuf::LiaisonConfig app_cfg)
    : Wt::WApplication(env), app_cfg_(app_cfg)
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
    WContainerWidget* header_div = new WContainerWidget(root());
    header_div->setId("header");

    WText* header = new WText(title_text, header_div);
    header->setId("header");

    WImage* goby_logo = new WImage("images/gobysoft_logo_dot_org_small.png");
    WAnchor* goby_logo_a =
        new WAnchor("http://gobysoft.org/#/software/goby", goby_logo, header_div);
    goby_logo_a->setId("goby_logo");
    goby_logo_a->setStyleClass("no_ul");
    goby_logo_a->setTarget(TargetNewWindow);

    if (app_cfg_.has_upper_right_logo())
    {
        WImage* goby_lp_image = new WImage(app_cfg_.upper_right_logo());
        WAnchor* goby_lp_image_a = new WAnchor(
            app_cfg_.has_upper_right_logo_link() ? app_cfg_.upper_right_logo_link() : "",
            goby_lp_image, header_div);
        goby_lp_image_a->setId("lp_logo");
        goby_lp_image_a->setStyleClass("no_ul");
        goby_lp_image_a->setTarget(TargetNewWindow);
    }

    new WText("<hr/>", root());

    WContainerWidget* menu_div = new WContainerWidget(root());
    menu_div->setStyleClass("menu");

    WContainerWidget* contents_div = new WContainerWidget(root());
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

    add_to_menu(menu_, new LiaisonHome);
    add_to_menu(menu_, new LiaisonScope(app_cfg_));
    add_to_menu(menu_, new LiaisonCommander(app_cfg_));

    using liaison_load_func = std::vector<goby::zeromq::LiaisonContainer*> (*)(
        const protobuf::LiaisonConfig& cfg);

    for (int i = 0, n = Liaison::plugin_handles_.size(); i < n; ++i)
    {
        liaison_load_func liaison_load_ptr =
            (liaison_load_func)dlsym(Liaison::plugin_handles_[i], "goby3_liaison_load");

        if (liaison_load_ptr)
        {
            std::vector<goby::zeromq::LiaisonContainer*> containers = (*liaison_load_ptr)(app_cfg_);
            for (int j = 0, m = containers.size(); j < m; ++j) add_to_menu(menu_, containers[j]);
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
    for (int i = 0, n = items.size(); i < n; ++i)
    {
        LiaisonContainer* contents = menu_contents_[items[i]];
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
    for (int i = 0, n = items.size(); i < n; ++i)
    {
        if (items[i] != item)
        {
            LiaisonContainer* other_contents = menu_contents_[items[i]];
            if (other_contents)
            {
                glog.is(DEBUG1) && glog << "Liaison: Unfocused : " << other_contents->name()
                                        << std::endl;
                other_contents->unfocus();
            }
        }
    }
}
