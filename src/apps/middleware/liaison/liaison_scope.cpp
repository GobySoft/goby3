// Copyright 2009-2018 Toby Schneider (http://gobysoft.org/index.wt/people/toby)
//                     GobySoft, LLC (2013-)
//                     Massachusetts Institute of Technology (2007-2014)
//                     Community contributors (see AUTHORS file)
//
//
// This file is part of the Goby Underwater Autonomy Project Libraries
// ("The Goby Libraries").
//
// The Goby Libraries are free software: you can redistribute them and/or modify
// them under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 2.1 of the License, or
// (at your option) any later version.
//
// The Goby Libraries are distributed in the hope that they will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with Goby.  If not, see <http://www.gnu.org/licenses/>.

#include "liaison_scope.h"

#include <Wt/WStandardItem>
#include <Wt/WPanel>
#include <Wt/WTextArea>
#include <Wt/WAnchor>
#include <Wt/WLineEdit>
#include <Wt/WPushButton>
#include <Wt/WVBoxLayout>
#include <Wt/WStringListModel>
#include <Wt/WSortFilterProxyModel>
#include <Wt/WSelectionBox>
#include <Wt/WTable>
#include <Wt/WTimer>
#include <Wt/Chart/WCartesianChart>
#include <Wt/WDateTime>
#include <Wt/WApplication>

#include <boost/regex.hpp>
#include <boost/algorithm/string/regex.hpp>

#include "goby/common/time.h"

using namespace Wt;
using namespace goby::common::logger_lock;
using namespace goby::common::logger;

goby::common::LiaisonScope::LiaisonScope(const protobuf::LiaisonConfig& cfg)
    : LiaisonContainerWithComms<LiaisonScope, ScopeCommsThread>(cfg),
      pb_scope_config_(cfg.pb_scope_config()),
      history_model_(new Wt::WStringListModel(this)),
      model_(new LiaisonScopeProtobufModel(pb_scope_config_, this)),
      proxy_(new Wt::WSortFilterProxyModel(this)),
      main_layout_(new Wt::WVBoxLayout(this)),
      last_scope_state_(UNKNOWN),
      subscriptions_div_(new SubscriptionsContainer(this, model_, history_model_, msg_map_)),
      history_header_div_(new HistoryContainer(main_layout_, history_model_, pb_scope_config_)),
      controls_div_(new ControlsContainer(&scope_timer_, cfg.start_paused(),
                                          this, subscriptions_div_, history_header_div_)),
      regex_filter_div_(new RegexFilterContainer(model_, proxy_, pb_scope_config_)),
      scope_tree_view_(new LiaisonScopeProtobufTreeView(pb_scope_config_)),
      bottom_fill_(new WContainerWidget)
{


    this->resize(WLength::Auto, WLength(100, WLength::Percentage));
    

    setStyleClass("scope");

    proxy_->setSourceModel(model_);
    scope_tree_view_->setModel(proxy_);    
    scope_tree_view_->sortByColumn(pb_scope_config_.sort_by_column(),
                                   pb_scope_config_.sort_ascending() ?
                                   AscendingOrder : DescendingOrder);

    
    main_layout_->addWidget(controls_div_);
    main_layout_->addWidget(subscriptions_div_);
    main_layout_->addWidget(history_header_div_);
    main_layout_->addWidget(regex_filter_div_);
    main_layout_->addWidget(scope_tree_view_);
    main_layout_->setResizable(main_layout_->count()-1);    
    main_layout_->addWidget(bottom_fill_, -1, AlignTop);
    main_layout_->addStretch(1);
    bottom_fill_->resize(WLength::Auto, 100);
    
    for(int i = 0, n = pb_scope_config_.subscription_size(); i < n; ++i)
        subscriptions_div_->add_subscription(pb_scope_config_.subscription(i));

    for(int i = 0, n = pb_scope_config_.history_size(); i < n; ++i)
        history_header_div_->add_history(pb_scope_config_.history(i));

    
    wApp->globalKeyPressed().connect(this, &LiaisonScope::handle_global_key);

    scope_timer_.setInterval(1/cfg.update_freq()*1.0e3);
    scope_timer_.timeout().connect(this, &LiaisonScope::loop);

    set_name("Scope");
}

void goby::common::LiaisonScope::loop()
{
    glog.is(DEBUG2) && glog << "LiaisonScope: polling" << std::endl;
}



void goby::common::LiaisonScope::attach_pb_rows(const std::vector<Wt::WStandardItem *>& items,
                                                const google::protobuf::Message& pb_msg)
{

    Wt::WStandardItem* key_item = items[protobuf::ProtobufScopeConfig::COLUMN_KEY];
    
    
    std::vector<std::string> result;
    std::string debug_string = pb_msg.DebugString();
    boost::trim(debug_string);
            
    boost::split(result, debug_string, boost::is_any_of("\n"));
    
    key_item->setRowCount(result.size());
    key_item->setColumnCount(protobuf::ProtobufScopeConfig::COLUMN_MAX + 1);

    for(int i = 0, n = result.size(); i < n; ++i)
    {
        for(int j = 0; j <= protobuf::ProtobufScopeConfig::COLUMN_MAX; ++j)
        {
            if(!key_item->child(i, j))
                key_item->setChild(i, j, new Wt::WStandardItem);

            if(j == protobuf::ProtobufScopeConfig::COLUMN_VALUE)
            {
                key_item->child(i,j)->setText(result[i]);
            }
            else
            {
                // so we can still sort by these fields
                key_item->child(i, j)->setText(items[j]->text());
                key_item->child(i, j)->setStyleClass("invisible");    
            }
        }
    }
}

std::vector<Wt::WStandardItem *> goby::common::LiaisonScope::create_row(const std::string& group, const google::protobuf::Message& msg)
{
    std::vector<Wt::WStandardItem *> items;
    for(int i = 0; i <= protobuf::ProtobufScopeConfig::COLUMN_MAX; ++i)
        items.push_back(new WStandardItem);
    update_row(group, msg, items);

    return items;
}



void goby::common::LiaisonScope::update_row(const std::string& group, const google::protobuf::Message& msg, const std::vector<WStandardItem *>& items)
{
    items[protobuf::ProtobufScopeConfig::COLUMN_KEY]->setText(group);

    items[protobuf::ProtobufScopeConfig::COLUMN_TYPE]->setText(msg.GetDescriptor()->full_name());
        
    items[protobuf::ProtobufScopeConfig::COLUMN_VALUE]->setData(msg.ShortDebugString(), DisplayRole);

    
    items[protobuf::ProtobufScopeConfig::COLUMN_TIME]->setData(WDateTime::fromPosixTime(goby::time::to_ptime(goby::time::now())), DisplayRole);
    
    attach_pb_rows(items, msg);
}

void goby::common::LiaisonScope::handle_global_key(Wt::WKeyEvent event)
{
    switch(event.key())
    {
        // pull single update to display
        case Key_Enter:
            subscriptions_div_->refresh_with_newest();
            history_header_div_->flush_buffer();
            break;

            // toggle play/pause
        case Key_P:
            controls_div_->handle_play_pause(true);
            
        default:
            break;
    }

}


void goby::common::LiaisonScope::pause()
{
    controls_div_->pause();
}

void goby::common::LiaisonScope::resume()
{
    controls_div_->resume();
}

void goby::common::LiaisonScope::inbox(const std::string& group, std::shared_ptr<const google::protobuf::Message> msg)
{
    if(is_paused())
    {
        std::map<std::string, HistoryContainer::MVC>::iterator hist_it =
            history_header_div_->history_models_.find(group);
        if(hist_it != history_header_div_->history_models_.end())
        {
            // buffer for later display
            history_header_div_->buffer_.push_back(std::make_pair(group, msg));
        }
    }
    else
    {
        handle_message(group, *msg, true);
    }
}



void goby::common::LiaisonScope::handle_message(const std::string& group,
                                                const google::protobuf::Message& msg,
                                                bool fresh_message)
{    
//    glog.is(DEBUG1) && glog << "LiaisonScope: got message:  " << msg << std::endl;  
    std::map<std::string, int>::iterator it = msg_map_.find(group);
    if(it != msg_map_.end())
    {
        std::vector<WStandardItem*> items;
        items.push_back(model_->item(it->second, protobuf::ProtobufScopeConfig::COLUMN_KEY));
        items.push_back(model_->item(it->second, protobuf::ProtobufScopeConfig::COLUMN_TYPE));
        items.push_back(model_->item(it->second, protobuf::ProtobufScopeConfig::COLUMN_VALUE));
        items.push_back(model_->item(it->second, protobuf::ProtobufScopeConfig::COLUMN_TIME));
        update_row(group, msg, items);
    }
    else
    {
        std::vector<WStandardItem*> items = create_row(group, msg);
        msg_map_.insert(make_pair(group, model_->rowCount()));
        model_->appendRow(items);
        history_model_->addString(group);
        history_model_->sort(0);
        regex_filter_div_->handle_set_regex_filter();
    }

    if(fresh_message)
    {
        history_header_div_->display_message(group, msg);
    }
}




goby::common::LiaisonScopeProtobufTreeView::LiaisonScopeProtobufTreeView(const protobuf::ProtobufScopeConfig& pb_scope_config , Wt::WContainerWidget* parent /*= 0*/)
    : WTreeView(parent)
{
    this->setAlternatingRowColors(true);

    this->setColumnWidth(protobuf::ProtobufScopeConfig::COLUMN_KEY, pb_scope_config.column_width().key_width());
    this->setColumnWidth(protobuf::ProtobufScopeConfig::COLUMN_TYPE, pb_scope_config.column_width().type_width());
    this->setColumnWidth(protobuf::ProtobufScopeConfig::COLUMN_VALUE, pb_scope_config.column_width().value_width());
    this->setColumnWidth(protobuf::ProtobufScopeConfig::COLUMN_TIME, pb_scope_config.column_width().time_width());

    this->resize(Wt::WLength::Auto,
                 pb_scope_config.scope_height());

    this->setMinimumSize(pb_scope_config.column_width().key_width()+
                         pb_scope_config.column_width().type_width()+
                         pb_scope_config.column_width().value_width()+
                         pb_scope_config.column_width().time_width()+
                         7*(protobuf::ProtobufScopeConfig::COLUMN_MAX+1),
                         Wt::WLength::Auto);    

//    this->doubleClicked().connect(this, &LiaisonScopeProtobufTreeView::handle_double_click);
}

 // void goby::common::LiaisonScopeProtobufTreeView::handle_double_click(const Wt::WModelIndex& proxy_index, const Wt::WMouseEvent& event)
// {

     
//     const Wt::WAbstractProxyModel* proxy = dynamic_cast<const Wt::WAbstractProxyModel*>(this->model());
//     const Wt::WStandardItemModel* model = dynamic_cast<Wt::WStandardItemModel*>(proxy->sourceModel());
//     WModelIndex model_index = proxy->mapToSource(proxy_index);

//     glog.is(DEBUG1) && glog << "clicked: " << model_index.row() << "," << model_index.column() << std::endl;    
    
//     attach_pb_rows(model->item(model_index.row(), protobuf::ProtobufScopeConfig::COLUMN_KEY),
//                    model->item(model_index.row(), protobuf::ProtobufScopeConfig::COLUMN_VALUE)->text().narrow());

//     this->setExpanded(proxy_index, true);
// }





goby::common::LiaisonScopeProtobufModel::LiaisonScopeProtobufModel(const protobuf::ProtobufScopeConfig& pb_scope_config, Wt::WContainerWidget* parent /*= 0*/)
    : WStandardItemModel(0, protobuf::ProtobufScopeConfig::COLUMN_MAX+1, parent)
{
    this->setHeaderData(protobuf::ProtobufScopeConfig::COLUMN_KEY, Horizontal, std::string("Group"));
    this->setHeaderData(protobuf::ProtobufScopeConfig::COLUMN_TYPE, Horizontal, std::string("Protobuf Type"));
    this->setHeaderData(protobuf::ProtobufScopeConfig::COLUMN_VALUE, Horizontal, std::string("Value"));
    this->setHeaderData(protobuf::ProtobufScopeConfig::COLUMN_TIME, Horizontal, std::string("Time"));

}


goby::common::LiaisonScope::ControlsContainer::ControlsContainer(Wt::WTimer* timer,
                                                                 bool start_paused,
                                                                 LiaisonScope* scope,
                                                                 SubscriptionsContainer* subscriptions_div,
                                                                 HistoryContainer* history_header_div,
                                                                 Wt::WContainerWidget* parent /*= 0*/)
    : Wt::WContainerWidget(parent),
      timer_(timer),
      play_pause_button_(new WPushButton("Play/Pause [p]", this)),
      spacer_(new Wt::WText(" ", this)),
      play_state_(new Wt::WText(this)),
      is_paused_(start_paused),
      scope_(scope),
      subscriptions_div_(subscriptions_div),
      history_header_div_(history_header_div)
{
    play_pause_button_->clicked().connect(boost::bind(&ControlsContainer::handle_play_pause, this, true));

    handle_play_pause(false);
}

goby::common::LiaisonScope::ControlsContainer::~ControlsContainer()
{
    // stop the paused mail thread before destroying
    is_paused_ = false;
    //    if(paused_mail_thread_ && paused_mail_thread_->joinable())
    //        paused_mail_thread_->join();
}

void goby::common::LiaisonScope::ControlsContainer::handle_play_pause(bool toggle_state)
{
    if(toggle_state)
        is_paused_ = !(is_paused_);

    is_paused_ ? pause() : resume();
    play_state_->setText(is_paused_ ? "Paused ([enter] refreshes). " : "Playing... ");
}

void goby::common::LiaisonScope::ControlsContainer::pause()
{
    // stop the Wt timer and pass control over to a local thread
    // (so we don't stop reading mail)
    timer_->stop();
    is_paused_ = true;
    // paused_mail_thread_.reset(new boost::thread(boost::bind(&ControlsContainer::run_paused_mail, this)));
}

void goby::common::LiaisonScope::ControlsContainer::resume()
{
    // stop the local thread and pass control over to a Wt
    is_paused_ = false;
    //    if(paused_mail_thread_ && paused_mail_thread_->joinable())
    //        paused_mail_thread_->join();
    timer_->start();

    // update with changes since the last we were playing
    subscriptions_div_->refresh_with_newest();
    history_header_div_->flush_buffer();
}

void goby::common::LiaisonScope::ControlsContainer::run_paused_mail()
{
    while(is_paused_)
    {
        // while(scope_->zeromq_service_->poll(10000))
        // { }
    }
}

goby::common::LiaisonScope::SubscriptionsContainer::SubscriptionsContainer(
    LiaisonScope* node,
    Wt::WStandardItemModel* model,
    Wt::WStringListModel* history_model,
    std::map<std::string, int>& msg_map,
    Wt::WContainerWidget* parent /*= 0*/)
    : WContainerWidget(parent),
      node_(node),
      model_(model),
      history_model_(history_model),
      msg_map_(msg_map),
      add_text_(new WText("Add subscription (e.g. NAV* or NAV_X): ", this)),
      subscribe_filter_text_(new WLineEdit(this)),
      subscribe_filter_button_(new WPushButton("Apply", this)),
      subscribe_break_(new WBreak(this)),
      remove_text_(new WText("Subscriptions (click to remove): ", this))
{
    subscribe_filter_button_->clicked().connect(this, &SubscriptionsContainer::handle_add_subscription);
    subscribe_filter_text_->enterPressed().connect(this, &SubscriptionsContainer::handle_add_subscription);
}


void goby::common::LiaisonScope::SubscriptionsContainer::handle_add_subscription()
{    
    add_subscription(subscribe_filter_text_->text().narrow());
    subscribe_filter_text_->setText("");
}

void goby::common::LiaisonScope::SubscriptionsContainer::add_subscription(std::string type)
{
    boost::trim(type);
    if(type.empty() || subscriptions_.count(type))
        return;
    
    WPushButton* new_button = new WPushButton(this);

    new_button->setText(type + " ");
    // node_->subscribe(type, LIAISON_INTERNAL_SUBSCRIBE_SOCKET);
    
    new_button->clicked().connect(boost::bind(&SubscriptionsContainer::handle_remove_subscription, this, new_button));

    subscriptions_.insert(type);

    refresh_with_newest(type);
}


void goby::common::LiaisonScope::SubscriptionsContainer::refresh_with_newest()
{   
    for(std::set<std::string>::const_iterator it = subscriptions_.begin(),
            end = subscriptions_.end(); it != end; ++it)
        refresh_with_newest(*it);
}

void goby::common::LiaisonScope::SubscriptionsContainer::refresh_with_newest(const std::string& type)
{   
    //    std::vector<CMOOSMsg> newest = node_->newest_substr(type);
    //    for(std::vector<CMOOSMsg>::iterator it = newest.begin(), end = newest.end();
    //        it != end; ++it)
    //    {
    //        node_->handle_message(*it, false);
    //    }
}



void goby::common::LiaisonScope::SubscriptionsContainer::handle_remove_subscription(WPushButton* clicked_button)
{
    std::string type_name = clicked_button->text().narrow();
    boost::trim(type_name);
    unsigned type_name_size = type_name.size();
    
    //    node_->unsubscribe(clicked_button->text().narrow(), LIAISON_INTERNAL_SUBSCRIBE_SOCKET);
    subscriptions_.erase(type_name);

    
    bool has_wildcard_ending = (type_name[type_name_size - 1] == '*');
    if(has_wildcard_ending)
        type_name = type_name.substr(0, type_name_size-1);

    for(int i = model_->rowCount()-1, n = 0; i >= n; --i)
    {
        std::string text_to_match = model_->item(i, 0)->text().narrow();
        boost::trim(text_to_match);
        
        bool remove = false;
        if(has_wildcard_ending && boost::starts_with(text_to_match, type_name))
            remove = true;
        else if(!has_wildcard_ending && boost::equals(text_to_match, type_name))
            remove = true;

        if(remove)
        {            
            history_model_->removeRows(msg_map_[text_to_match], 1);
            msg_map_.erase(text_to_match);
            glog.is(DEBUG1) && glog << "LiaisonScope: removed " << text_to_match << std::endl;            
            model_->removeRow(i);
            
            // shift down the remaining indices
            for(std::map<std::string, int>::iterator it = msg_map_.begin(),
                    n = msg_map_.end();
                it != n; ++it)
            {
                if(it->second > i)
                    --it->second;
            }            
        }
    }


    this->removeWidget(clicked_button);
    delete clicked_button; // removeWidget does not delete
}

goby::common::LiaisonScope::HistoryContainer::HistoryContainer(Wt::WVBoxLayout* main_layout,
                                                               Wt::WAbstractItemModel* model,
                                                               const protobuf::ProtobufScopeConfig& pb_scope_config,
                                                               Wt::WContainerWidget* parent /* = 0 */)
    : Wt::WContainerWidget(parent),
      main_layout_(main_layout),
      pb_scope_config_(pb_scope_config),
      hr_(new WText("<hr />", this)),
      add_text_(new WText(("Add history for key: "), this)),
      history_box_(new WComboBox(this)),
      history_button_(new WPushButton("Add", this)),
      buffer_(pb_scope_config.max_history_items())

{
    history_box_->setModel(model);
    history_button_->clicked().connect(this, &HistoryContainer::handle_add_history);
}

void goby::common::LiaisonScope::HistoryContainer::handle_add_history()
{
    std::string selected_key = history_box_->currentText().narrow();
    goby::common::protobuf::ProtobufScopeConfig::HistoryConfig config;
    config.set_key(selected_key);
    add_history(config);
}

void goby::common::LiaisonScope::HistoryContainer::add_history(const goby::common::protobuf::ProtobufScopeConfig::HistoryConfig& config)
{
    const std::string& selected_key = config.key();
    
    if(!history_models_.count(selected_key))
    {
        Wt::WContainerWidget* new_container = new WContainerWidget;

        Wt::WContainerWidget* text_container = new WContainerWidget(new_container);
        new WText("History for  ", text_container);
        WPushButton* remove_history_button = new WPushButton(selected_key, text_container);

        remove_history_button->clicked().connect(
            boost::bind(&HistoryContainer::handle_remove_history, this, selected_key));
        
        new WText(" (click to remove)", text_container);
        new WBreak(text_container);
        // WPushButton* toggle_plot_button = new WPushButton("Plot", text_container);

        
//        text_container->resize(Wt::WLength::Auto, WLength(4, WLength::FontEm));

        Wt::WStandardItemModel* new_model = new LiaisonScopeProtobufModel(pb_scope_config_,
                                                                      new_container);
        
        Wt::WSortFilterProxyModel* new_proxy = new Wt::WSortFilterProxyModel(new_container);
        new_proxy->setSourceModel(new_model);

        
        // Chart::WCartesianChart* chart = new Chart::WCartesianChart(new_container);
        // toggle_plot_button->clicked().connect(
        //     boost::bind(&HistoryContainer::toggle_history_plot, this, chart));
        // chart->setModel(new_model);    
        // chart->setXSeriesColumn(protobuf::ProtobufScopeConfig::COLUMN_TIME); 
        // Chart::WDataSeries s(protobuf::ProtobufScopeConfig::COLUMN_VALUE, Chart::LineSeries);
        // chart->addSeries(s);        
        
        // chart->setType(Chart::ScatterPlot);
        // chart->axis(Chart::XAxis).setScale(Chart::DateTimeScale); 
        // chart->axis(Chart::XAxis).setTitle("Time"); 
        // chart->axis(Chart::YAxis).setTitle(selected_key); 

        // WFont font;
        // font.setFamily(WFont::Serif, "Gentium");
        // chart->axis(Chart::XAxis).setTitleFont(font); 
        // chart->axis(Chart::YAxis).setTitleFont(font); 

        
        // // Provide space for the X and Y axis and title. 
        // chart->setPlotAreaPadding(80, Left);
        // chart->setPlotAreaPadding(40, Top | Bottom);
        // chart->setMargin(10, Top | Bottom);            // add margin vertically
        // chart->setMargin(WLength::Auto, Left | Right); // center horizontally
        // chart->resize(config.plot_width(), config.plot_height());

        // if(!config.show_plot())
        //     chart->hide();
        
        Wt::WTreeView* new_tree = new LiaisonScopeProtobufTreeView(pb_scope_config_, new_container);        
        main_layout_->insertWidget(main_layout_->count()-2, new_container);
        // set the widget *before* the one we just inserted to be resizable
        main_layout_->setResizable(main_layout_->count()-3);

        //main_layout_->insertWidget(main_layout_->count()-2, new_tree);
        // set the widget *before* the one we just inserted to be resizable
        //main_layout_->setResizable(main_layout_->count()-3);
        
        new_tree->setModel(new_proxy);
        MVC& mvc = history_models_[selected_key];
        mvc.key = selected_key;
        mvc.container = new_container;
        mvc.model = new_model;
        mvc.tree = new_tree;
        mvc.proxy = new_proxy;


        new_proxy->setFilterRegExp(".*");
        new_tree->sortByColumn(protobuf::ProtobufScopeConfig::COLUMN_TIME,
                               DescendingOrder);

    }
}

void goby::common::LiaisonScope::HistoryContainer::handle_remove_history(std::string key)
{
    glog.is(DEBUG2) && glog << "LiaisonScope: removing history for: " << key << std::endl;    
    
    main_layout_->removeWidget(history_models_[key].container);    
    main_layout_->removeWidget(history_models_[key].tree);

    history_models_.erase(key);
}

void goby::common::LiaisonScope::HistoryContainer::toggle_history_plot(Wt::WWidget* plot)
{
    if(plot->isHidden())
        plot->show();
    else
        plot->hide();
}

void goby::common::LiaisonScope::HistoryContainer::display_message(const std::string& group, const google::protobuf::Message& msg)
{
    std::map<std::string, HistoryContainer::MVC>::iterator hist_it =
        history_models_.find(group);
    if(hist_it != history_models_.end())
    {
        hist_it->second.model->appendRow(create_row(group, msg));
        while(hist_it->second.model->rowCount() > pb_scope_config_.max_history_items())
            hist_it->second.model->removeRow(0);
        
        hist_it->second.proxy->setFilterRegExp(".*");
    }
}

void goby::common::LiaisonScope::HistoryContainer::flush_buffer()
{
    for(const auto& p : buffer_)
        display_message(p.first, *p.second);
    buffer_.clear();
}



goby::common::LiaisonScope::RegexFilterContainer::RegexFilterContainer(
    Wt::WStandardItemModel* model,
    Wt::WSortFilterProxyModel* proxy,
    const protobuf::ProtobufScopeConfig& pb_scope_config,
    Wt::WContainerWidget* parent /* = 0 */)
    : Wt::WContainerWidget(parent),
      model_(model),
      proxy_(proxy),
      hr_(new WText("<hr />", this)),
      set_text_(new WText(("Set regex filter: Column: "), this)),
      regex_column_select_(new Wt::WComboBox(this)),
      expression_text_(new WText((" Expression: "), this)),
      regex_filter_text_(new WLineEdit(pb_scope_config.regex_filter_expression(), this)),
      regex_filter_button_(new WPushButton("Set", this)),
      regex_filter_clear_(new WPushButton("Clear", this))
{     
    for(int i = 0, n = model_->columnCount(); i < n; ++i)
        regex_column_select_->addItem(boost::any_cast<std::string>(model_->headerData(i)));
    regex_column_select_->setCurrentIndex(pb_scope_config.regex_filter_column());
    
    regex_filter_button_->clicked().connect(this, &RegexFilterContainer::handle_set_regex_filter);
    regex_filter_clear_->clicked().connect(this, &RegexFilterContainer::handle_clear_regex_filter);
    regex_filter_text_->enterPressed().connect(this, &RegexFilterContainer::handle_set_regex_filter);

    handle_set_regex_filter();
}


void goby::common::LiaisonScope::RegexFilterContainer::handle_set_regex_filter()
{
    proxy_->setFilterKeyColumn(regex_column_select_->currentIndex());
    proxy_->setFilterRegExp(regex_filter_text_->text());
}


void goby::common::LiaisonScope::RegexFilterContainer::handle_clear_regex_filter()
{
    regex_filter_text_->setText(".*");
    handle_set_regex_filter();
}

                          
                          
