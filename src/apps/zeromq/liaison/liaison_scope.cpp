// Copyright 2011-2020:
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

#include "liaison_scope.h"

#include <Wt/Chart/WCartesianChart>
#include <Wt/WAnchor>
#include <Wt/WApplication>
#include <Wt/WDateTime>
#include <Wt/WGroupBox>
#include <Wt/WLineEdit>
#include <Wt/WPanel>
#include <Wt/WPushButton>
#include <Wt/WSelectionBox>
#include <Wt/WSortFilterProxyModel>
#include <Wt/WStandardItem>
#include <Wt/WStringListModel>
#include <Wt/WTable>
#include <Wt/WTextArea>
#include <Wt/WTimer>
#include <Wt/WVBoxLayout>

#include "goby/time.h"

using namespace Wt;
using namespace goby::util::logger_lock;
using namespace goby::util::logger;

goby::apps::zeromq::LiaisonScope::LiaisonScope(const protobuf::LiaisonConfig& cfg)
    : LiaisonContainerWithComms<LiaisonScope, ScopeCommsThread>(cfg),
      pb_scope_config_(cfg.pb_scope_config()),
      history_model_(new Wt::WStringListModel(this)),
      model_(new LiaisonScopeProtobufModel(pb_scope_config_, this)),
      proxy_(new Wt::WSortFilterProxyModel(this)),
      main_layout_(new Wt::WVBoxLayout(this)),
      last_scope_state_(UNKNOWN),
      main_box_(new WGroupBox("Interprocess Messages")),
      subscriptions_div_(new SubscriptionsContainer(model_, history_model_, msg_map_, main_box_)),
      controls_div_(new ControlsContainer(&scope_timer_, cfg.start_paused(), this,
                                          subscriptions_div_, main_box_)),
      history_header_div_(
          new HistoryContainer(main_layout_, history_model_, pb_scope_config_, this, main_box_)),
      regex_filter_div_(new RegexFilterContainer(model_, proxy_, pb_scope_config_, main_box_)),
      scope_tree_view_(new LiaisonScopeProtobufTreeView(
          pb_scope_config_, pb_scope_config_.scope_height(), main_box_)),
      bottom_fill_(new WContainerWidget)
{
    //    this->resize(WLength::Auto, WLength(100, WLength::Percentage));

    setStyleClass("scope");

    proxy_->setSourceModel(model_);
    scope_tree_view_->setModel(proxy_);
    scope_tree_view_->sortByColumn(pb_scope_config_.sort_by_column(),
                                   pb_scope_config_.sort_ascending() ? AscendingOrder
                                                                     : DescendingOrder);

    main_layout_->addWidget(main_box_);
    //    main_layout_->setResizable(main_layout_->count()-1);
    //    main_layout_->addWidget(bottom_fill_, -1, AlignTop);
    //    main_layout_->addStretch(1);
    //    bottom_fill_->resize(WLength::Auto, 100);

    for (int i = 0, n = pb_scope_config_.history_size(); i < n; ++i)
        history_header_div_->add_history(pb_scope_config_.history(i));

    wApp->globalKeyPressed().connect(this, &LiaisonScope::handle_global_key);

    scope_timer_.setInterval(1 / cfg.update_freq() * 1.0e3);
    scope_timer_.timeout().connect(this, &LiaisonScope::loop);

    set_name("Scope");
}

void goby::apps::zeromq::LiaisonScope::loop()
{
    glog.is(DEBUG2) && glog << "LiaisonScope: polling" << std::endl;
}

void goby::apps::zeromq::LiaisonScope::attach_pb_rows(const std::vector<Wt::WStandardItem*>& items,
                                                const google::protobuf::Message& pb_msg)
{
    Wt::WStandardItem* key_item = items[protobuf::ProtobufScopeConfig::COLUMN_GROUP];

    std::vector<std::string> result;
    std::string debug_string = pb_msg.DebugString();
    boost::trim(debug_string);

    boost::split(result, debug_string, boost::is_any_of("\n"));

    key_item->setRowCount(result.size());
    key_item->setColumnCount(protobuf::ProtobufScopeConfig::COLUMN_MAX + 1);

    for (int i = 0, n = result.size(); i < n; ++i)
    {
        for (int j = 0; j <= protobuf::ProtobufScopeConfig::COLUMN_MAX; ++j)
        {
            if (!key_item->child(i, j))
                key_item->setChild(i, j, new Wt::WStandardItem);

            if (j == protobuf::ProtobufScopeConfig::COLUMN_VALUE)
            {
                key_item->child(i, j)->setText(result[i]);
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

std::vector<Wt::WStandardItem*>
goby::apps::zeromq::LiaisonScope::create_row(const std::string& group,
                                       const google::protobuf::Message& msg, bool do_attach_pb_rows)
{
    std::vector<Wt::WStandardItem*> items;
    for (int i = 0; i <= protobuf::ProtobufScopeConfig::COLUMN_MAX; ++i)
        items.push_back(new WStandardItem);
    update_row(group, msg, items, do_attach_pb_rows);

    return items;
}

void goby::apps::zeromq::LiaisonScope::update_row(const std::string& group,
                                            const google::protobuf::Message& msg,
                                            const std::vector<WStandardItem*>& items,
                                            bool do_attach_pb_rows)
{
    items[protobuf::ProtobufScopeConfig::COLUMN_GROUP]->setText(group);

    items[protobuf::ProtobufScopeConfig::COLUMN_TYPE]->setText(msg.GetDescriptor()->full_name());

    items[protobuf::ProtobufScopeConfig::COLUMN_VALUE]->setData(msg.ShortDebugString(),
                                                                DisplayRole);

    items[protobuf::ProtobufScopeConfig::COLUMN_TIME]->setData(
        WDateTime::fromPosixTime(goby::time::SystemClock::now<boost::posix_time::ptime>()),
        DisplayRole);

    if (do_attach_pb_rows)
        attach_pb_rows(items, msg);
}

void goby::apps::zeromq::LiaisonScope::handle_global_key(const Wt::WKeyEvent& event)
{
    switch (event.key())
    {
        // pull single update to display
        case Key_R:
            for (const auto& p : paused_buffer_) handle_message(p.first, *p.second, false);
            history_header_div_->flush_buffer();
            break;

            // toggle play/pause
        case Key_P: controls_div_->handle_play_pause(true); break;

        default: break;
    }
}

void goby::apps::zeromq::LiaisonScope::pause() { controls_div_->pause(); }

void goby::apps::zeromq::LiaisonScope::resume()
{
    controls_div_->resume();
    // update with changes since the last we were playing

    for (const auto& p : paused_buffer_) handle_message(p.first, *p.second, false);

    history_header_div_->flush_buffer();
}

void goby::apps::zeromq::LiaisonScope::inbox(
    const std::string& group, const std::shared_ptr<const google::protobuf::Message>& msg)
{
    if (is_paused())
    {
        auto hist_it = history_header_div_->history_models_.find(group);
        if (hist_it != history_header_div_->history_models_.end())
        {
            // buffer for later display
            history_header_div_->buffer_.push_back(std::make_pair(group, msg));
        }

        paused_buffer_[group] = msg;
    }
    else
    {
        handle_message(group, *msg, true);
    }
}

void goby::apps::zeromq::LiaisonScope::handle_message(const std::string& group,
                                                const google::protobuf::Message& msg,
                                                bool fresh_message)
{
    //    glog.is(DEBUG1) && glog << "LiaisonScope: got message:  " << msg << std::endl;
    auto it = msg_map_.find(group);
    if (it != msg_map_.end())
    {
        std::vector<WStandardItem*> items;
        items.push_back(model_->item(it->second, protobuf::ProtobufScopeConfig::COLUMN_GROUP));
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

    if (fresh_message)
    {
        history_header_div_->display_message(group, msg);
    }
}

goby::apps::zeromq::LiaisonScopeProtobufTreeView::LiaisonScopeProtobufTreeView(
    const protobuf::ProtobufScopeConfig& pb_scope_config, int scope_height,
    Wt::WContainerWidget* parent /*= 0*/)
    : WTreeView(parent)
{
    this->setAlternatingRowColors(true);

    this->setColumnWidth(protobuf::ProtobufScopeConfig::COLUMN_GROUP,
                         pb_scope_config.column_width().group_width());
    this->setColumnWidth(protobuf::ProtobufScopeConfig::COLUMN_TYPE,
                         pb_scope_config.column_width().type_width());
    this->setColumnWidth(protobuf::ProtobufScopeConfig::COLUMN_VALUE,
                         pb_scope_config.column_width().value_width());
    this->setColumnWidth(protobuf::ProtobufScopeConfig::COLUMN_TIME,
                         pb_scope_config.column_width().time_width());

    this->resize(Wt::WLength::Auto, scope_height);

    this->setMinimumSize(pb_scope_config.column_width().group_width() +
                             pb_scope_config.column_width().type_width() +
                             pb_scope_config.column_width().value_width() +
                             pb_scope_config.column_width().time_width() +
                             7 * (protobuf::ProtobufScopeConfig::COLUMN_MAX + 1),
                         Wt::WLength::Auto);

    //    this->doubleClicked().connect(this, &LiaisonScopeProtobufTreeView::handle_double_click);
}

// void goby::apps::zeromq::LiaisonScopeProtobufTreeView::handle_double_click(const Wt::WModelIndex& proxy_index, const Wt::WMouseEvent& event)
// {

//     const Wt::WAbstractProxyModel* proxy = dynamic_cast<const Wt::WAbstractProxyModel*>(this->model());
//     const Wt::WStandardItemModel* model = dynamic_cast<Wt::WStandardItemModel*>(proxy->sourceModel());
//     WModelIndex model_index = proxy->mapToSource(proxy_index);

//     glog.is(DEBUG1) && glog << "clicked: " << model_index.row() << "," << model_index.column() << std::endl;

//     attach_pb_rows(model->item(model_index.row(), protobuf::ProtobufScopeConfig::COLUMN_GROUP),
//                    model->item(model_index.row(), protobuf::ProtobufScopeConfig::COLUMN_VALUE)->text().narrow());

//     this->setExpanded(proxy_index, true);
// }

goby::apps::zeromq::LiaisonScopeProtobufModel::LiaisonScopeProtobufModel(
    const protobuf::ProtobufScopeConfig& /*pb_scope_config*/, Wt::WContainerWidget* parent /*= 0*/)
    : WStandardItemModel(0, protobuf::ProtobufScopeConfig::COLUMN_MAX + 1, parent)
{
    this->setHeaderData(protobuf::ProtobufScopeConfig::COLUMN_GROUP, Horizontal,
                        std::string("Group"));
    this->setHeaderData(protobuf::ProtobufScopeConfig::COLUMN_TYPE, Horizontal,
                        std::string("Protobuf Type"));
    this->setHeaderData(protobuf::ProtobufScopeConfig::COLUMN_VALUE, Horizontal,
                        std::string("Value"));
    this->setHeaderData(protobuf::ProtobufScopeConfig::COLUMN_TIME, Horizontal,
                        std::string("Time"));
}

goby::apps::zeromq::LiaisonScope::ControlsContainer::ControlsContainer(
    Wt::WTimer* timer, bool start_paused, LiaisonScope* scope,
    SubscriptionsContainer* subscriptions_div, Wt::WContainerWidget* parent)
    : Wt::WContainerWidget(parent),
      timer_(timer),
      play_pause_button_(new WPushButton("Play", this)),
      spacer_(new Wt::WText(" ", this)),
      play_state_(new Wt::WText(this)),
      is_paused_(start_paused),
      scope_(scope),
      subscriptions_div_(subscriptions_div)
{
    play_pause_button_->clicked().connect(
        boost::bind(&ControlsContainer::handle_play_pause, this, true));

    handle_play_pause(false);
}

goby::apps::zeromq::LiaisonScope::ControlsContainer::~ControlsContainer()
{
    // stop the paused mail thread before destroying
    is_paused_ = false;
    //    if(paused_mail_thread_ && paused_mail_thread_->joinable())
    //        paused_mail_thread_->join();
}

void goby::apps::zeromq::LiaisonScope::ControlsContainer::handle_play_pause(bool toggle_state)
{
    if (toggle_state)
        is_paused_ = !(is_paused_);

    is_paused_ ? pause() : resume();

    play_pause_button_->setText(is_paused_ ? "Play" : "Pause");

    play_state_->setText(is_paused_ ? "Paused ([r] for a single refresh / [p] to resume). "
                                    : "Playing... [p] to pause");
}

void goby::apps::zeromq::LiaisonScope::ControlsContainer::pause()
{
    // stop the Wt timer and pass control over to a local thread
    // (so we don't stop reading mail)
    timer_->stop();
    is_paused_ = true;
    // paused_mail_thread_.reset(new std::thread(boost::bind(&ControlsContainer::run_paused_mail, this)));
}

void goby::apps::zeromq::LiaisonScope::ControlsContainer::resume()
{
    is_paused_ = false;
    timer_->start();
}

goby::apps::zeromq::LiaisonScope::SubscriptionsContainer::SubscriptionsContainer(
    Wt::WStandardItemModel* model, Wt::WStringListModel* history_model,
    std::map<std::string, int>& msg_map, Wt::WContainerWidget* parent /*= 0*/)
    : WContainerWidget(parent), model_(model), history_model_(history_model), msg_map_(msg_map)
{
}

goby::apps::zeromq::LiaisonScope::HistoryContainer::HistoryContainer(
    Wt::WVBoxLayout* main_layout, Wt::WAbstractItemModel* model,
    const protobuf::ProtobufScopeConfig& pb_scope_config, LiaisonScope* scope,
    WContainerWidget* parent)
    : WContainerWidget(parent),
      main_layout_(main_layout),
      pb_scope_config_(pb_scope_config),
      hr_(new WText("<hr />", this)),
      add_text_(new WText(("Add history for group: "), this)),
      history_box_(new WComboBox(this)),
      history_button_(new WPushButton("Add", this)),
      buffer_(pb_scope_config.max_history_items()),
      scope_(scope)
{
    history_box_->setModel(model);
    history_button_->clicked().connect(this, &HistoryContainer::handle_add_history);
}

void goby::apps::zeromq::LiaisonScope::HistoryContainer::handle_add_history()
{
    std::string selected_key = history_box_->currentText().narrow();
    protobuf::ProtobufScopeConfig::HistoryConfig config;
    config.set_group(selected_key);
    add_history(config);
}

void goby::apps::zeromq::LiaisonScope::HistoryContainer::add_history(
    const protobuf::ProtobufScopeConfig::HistoryConfig& config)
{
    const std::string& selected_key = config.group();

    if (!history_models_.count(selected_key))
    {
        auto* new_container = new WGroupBox("History");

        auto* text_container = new WContainerWidget(new_container);
        auto* remove_history_button = new WPushButton(selected_key, text_container);

        remove_history_button->clicked().connect(
            boost::bind(&HistoryContainer::handle_remove_history, this, selected_key));

        new WText(" (click to remove)", text_container);
        new WBreak(text_container);
        // WPushButton* toggle_plot_button = new WPushButton("Plot", text_container);

        //        text_container->resize(Wt::WLength::Auto, WLength(4, WLength::FontEm));

        Wt::WStandardItemModel* new_model =
            new LiaisonScopeProtobufModel(pb_scope_config_, new_container);

        auto* new_proxy = new Wt::WSortFilterProxyModel(new_container);
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

        Wt::WTreeView* new_tree = new LiaisonScopeProtobufTreeView(
            pb_scope_config_, pb_scope_config_.history_height(), new_container);
        auto new_index = main_layout_->count();
        main_layout_->insertWidget(new_index, new_container);
        //        main_layout_->setResizable(new_index);
        // set the widget *before* the one we just inserted to be resizable

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
        new_tree->sortByColumn(protobuf::ProtobufScopeConfig::COLUMN_TIME, DescendingOrder);
    }
}

void goby::apps::zeromq::LiaisonScope::HistoryContainer::handle_remove_history(
    const std::string& key)
{
    glog.is(DEBUG2) && glog << "LiaisonScope: removing history for: " << key << std::endl;

    main_layout_->removeWidget(history_models_[key].container);
    // main_layout_->removeWidget(history_models_[key].tree);

    delete history_models_[key].container;

    history_models_.erase(key);
}

void goby::apps::zeromq::LiaisonScope::HistoryContainer::toggle_history_plot(Wt::WWidget* plot)
{
    if (plot->isHidden())
        plot->show();
    else
        plot->hide();
}

void goby::apps::zeromq::LiaisonScope::HistoryContainer::display_message(
    const std::string& group, const google::protobuf::Message& msg)
{
    auto hist_it = history_models_.find(group);
    if (hist_it != history_models_.end())
    {
        // when the pb_row children exist, Wt segfaults when removing the parent... for now don't attach pb_rows for history items
        hist_it->second.model->appendRow(scope_->create_row(group, msg, false));
        while (hist_it->second.model->rowCount() > pb_scope_config_.max_history_items())
        {
            int row_to_remove = 0;
            hist_it->second.model->removeRow(row_to_remove);
        }

        hist_it->second.proxy->setFilterRegExp(".*");
    }
}

void goby::apps::zeromq::LiaisonScope::HistoryContainer::flush_buffer()
{
    for (const auto& p : buffer_) display_message(p.first, *p.second);
    buffer_.clear();
}

goby::apps::zeromq::LiaisonScope::RegexFilterContainer::RegexFilterContainer(
    Wt::WStandardItemModel* model, Wt::WSortFilterProxyModel* proxy,
    const protobuf::ProtobufScopeConfig& pb_scope_config, Wt::WContainerWidget* parent /* = 0 */)
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
    for (int i = 0, n = model_->columnCount(); i < n; ++i)
        regex_column_select_->addItem(boost::any_cast<std::string>(model_->headerData(i)));
    regex_column_select_->setCurrentIndex(pb_scope_config.regex_filter_column());

    regex_filter_button_->clicked().connect(this, &RegexFilterContainer::handle_set_regex_filter);
    regex_filter_clear_->clicked().connect(this, &RegexFilterContainer::handle_clear_regex_filter);
    regex_filter_text_->enterPressed().connect(this,
                                               &RegexFilterContainer::handle_set_regex_filter);

    handle_set_regex_filter();
}

void goby::apps::zeromq::LiaisonScope::RegexFilterContainer::handle_set_regex_filter()
{
    proxy_->setFilterKeyColumn(regex_column_select_->currentIndex());
    proxy_->setFilterRegExp(regex_filter_text_->text());
}

void goby::apps::zeromq::LiaisonScope::RegexFilterContainer::handle_clear_regex_filter()
{
    regex_filter_text_->setText(".*");
    handle_set_regex_filter();
}
