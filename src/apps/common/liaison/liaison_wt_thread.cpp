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

#include <Wt/WText>
#include <Wt/WHBoxLayout>
#include <Wt/WVBoxLayout>
#include <Wt/WStackedWidget>
#include <Wt/WImage>
#include <Wt/WAnchor>

#include "goby/common/time.h"
#include "goby/util/dynamic_protobuf_manager.h"

#include "liaison_wt_thread.h"
#include "liaison_home.h"

#include "goby/moos/moos_liaison_load.h"


using goby::glog;
using namespace Wt;    
using namespace goby::common::logger;

goby::common::LiaisonWtThread::LiaisonWtThread(const Wt::WEnvironment& env)
    : Wt::WApplication(env)
{    
//    zeromq_service_.connect_inbox_slot(&LiaisonWtThread::inbox, this);

    Wt::WString title_text("goby liaison: " + liaison_cfg_.base().platform_name());
    setTitle(title_text);

    useStyleSheet(std::string("css/fonts.css?" + common::goby_file_timestamp()));
    useStyleSheet(std::string("css/liaison.css?" + common::goby_file_timestamp()));
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
    WAnchor* goby_logo_a = new WAnchor("http://gobysoft.org/#/software/goby", goby_logo, header_div);
    goby_logo_a->setId("goby_logo");
    goby_logo_a->setStyleClass("no_ul");
    goby_logo_a->setTarget(TargetNewWindow);

    if(!liaison_cfg_.has_upper_right_logo())
    {
        WImage* goby_lp_image = new WImage("images/mit-logo.gif");
        WAnchor* goby_lp_image_a = new WAnchor("http://lamss.mit.edu", goby_lp_image, header_div);
        goby_lp_image_a->setId("lp_logo");
        goby_lp_image_a->setStyleClass("no_ul");
        goby_lp_image_a->setTarget(TargetNewWindow);
    }
    else
    {
        WImage* goby_lp_image = new WImage(liaison_cfg_.upper_right_logo());
        WAnchor* goby_lp_image_a = new WAnchor(liaison_cfg_.has_upper_right_logo_link() ?
                                               liaison_cfg_.upper_right_logo_link() : "", goby_lp_image, header_div);
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


    typedef std::vector<goby::common::LiaisonContainer*> (*liaison_load_func)(const goby::common::protobuf::LiaisonConfig& cfg, boost::shared_ptr<zmq::context_t> zmq_context);

    for(int i = 0, n = Liaison::plugin_handles_.size(); i < n; ++i)
    {
        liaison_load_func liaison_load_ptr = (liaison_load_func) dlsym(Liaison::plugin_handles_[i], "goby_liaison_load");
            
        if(liaison_load_ptr)
        {
            std::vector<goby::common::LiaisonContainer*> containers = (*liaison_load_ptr)(liaison_cfg_, Liaison::zmq_context());
            for(int j = 0, m = containers.size(); j< m; ++j)
                add_to_menu(menu_, containers[j]);
        }
        else
        {
            glog.is(WARN) && glog << "Liaison: Cannot find function 'goby_liaison_load' in plugin library." << std::endl;
        }        
    }
   
    
    menu_->itemSelected().connect(this, &LiaisonWtThread::handle_menu_selection);

    handle_menu_selection(menu_->currentItem());
    
}

goby::common::LiaisonWtThread::~LiaisonWtThread()
{    
    // run on all children
    const std::vector< WMenuItem * >& items = menu_->items();
    for(int i = 0, n = items.size(); i < n; ++i)
    {
        LiaisonContainer* contents = menu_contents_[items[i]];
        if(contents)
        {
            glog.is(DEBUG1) && glog << "Liaison: Cleanup : " << contents->name() <<  std::endl;
            contents->cleanup();
        }
    }
}
            

void goby::common::LiaisonWtThread::add_to_menu(WMenu* menu, LiaisonContainer* container)
{
    Wt::WMenuItem* new_item = menu->addItem(container->name(), container);
    menu_contents_.insert(std::make_pair(new_item, container));
}

void goby::common::LiaisonWtThread::handle_menu_selection(Wt::WMenuItem * item)
{    
    
    LiaisonContainer* contents = menu_contents_[item];
    if(contents)
    {
        glog.is(DEBUG1) && glog << "Liaison: Focused : " << contents->name() <<  std::endl;

        contents->focus();
    }
    else
    {
        glog.is(WARN) && glog << "Liaison: Invalid menu item!" << std::endl;
    }
    
    // unfocus all others
    const std::vector< WMenuItem * >& items = menu_->items();
    for(int i = 0, n = items.size(); i < n; ++i)
    {
        if(items[i] != item)
        {
            LiaisonContainer* other_contents = menu_contents_[items[i]];
            if(other_contents)
            {
                glog.is(DEBUG1) && glog << "Liaison: Unfocused : " << other_contents->name() <<  std::endl;
                other_contents->unfocus();
            }
        }
    }    
}


void goby::common::LiaisonWtThread::inbox(MarshallingScheme marshalling_scheme,
                                                      const std::string& identifier,
                                                      const void* data,
                                                      int size,
                                                      int socket_id)
{
    glog.is(DEBUG1) && glog << "LiaisonWtThread: got message with identifier: " << identifier << std::endl;
}

