// Copyright 2011-2022:
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

#ifndef GOBY_APPS_ZEROMQ_LIAISON_LIAISON_SCOPE_H
#define GOBY_APPS_ZEROMQ_LIAISON_LIAISON_SCOPE_H

#include <algorithm>     // for max
#include <map>           // for operator!=, map
#include <memory>        // for shared_ptr, __sh...
#include <ostream>       // for operator<<, basi...
#include <set>           // for set
#include <string>        // for string
#include <unordered_map> // for unordered_map<>:...
#include <utility>       // for pair
#include <vector>        // for vector

#include <Wt/WContainerWidget>             // for WContainerWidget
#include <Wt/WEvent>                       // for WKeyEvent
#include <Wt/WStandardItemModel>           // for WStandardItemModel
#include <Wt/WTimer>                       // for WTimer
#include <Wt/WTreeView>                    // for WTreeView
#include <boost/circular_buffer.hpp>       // for circular_buffer
#include <boost/units/quantity.hpp>        // for operator/
#include <dccl/dynamic_protobuf_manager.h> // for DynamicProtobufM...
#include <google/protobuf/message.h>       // for Message

#include "goby/middleware/group.h"                  // for Group
#include "goby/middleware/marshalling/interface.h"  // for MarshallingScheme
#include "goby/util/debug_logger/flex_ostream.h"    // for operator<<, Flex...
#include "goby/zeromq/liaison/liaison_container.h"  // for LiaisonCommsThread
#include "goby/zeromq/protobuf/liaison_config.pb.h" // for LiaisonConfig (p...

namespace Wt
{
class WAbstractItemModel;
class WComboBox;
class WGroupBox;
class WLineEdit;
class WPushButton;
class WSortFilterProxyModel;
class WStandardItem;
class WStringListModel;
class WText;
class WVBoxLayout;
class WWidget;
class WDoubleSpinBox;
} // namespace Wt

namespace goby
{
namespace apps
{
namespace zeromq
{
class ScopeCommsThread;

class LiaisonScope : public goby::zeromq::LiaisonContainerWithComms<LiaisonScope, ScopeCommsThread>
{
  public:
    LiaisonScope(const protobuf::LiaisonConfig& cfg);

    void inbox(const std::string& group,
               const std::shared_ptr<const google::protobuf::Message>& msg);

    void handle_message(const std::string& group, const google::protobuf::Message& msg,
                        bool fresh_message);

    std::vector<Wt::WStandardItem*> create_row(const std::string& group,
                                               const google::protobuf::Message& msg,
                                               bool do_attach_pb_rows = true);
    void attach_pb_rows(const std::vector<Wt::WStandardItem*>& items,
                        const std::string& debug_string);

    void update_row(const std::string& group, const google::protobuf::Message& msg,
                    const std::vector<Wt::WStandardItem*>& items, bool do_attach_pb_rows = true);

    void update_freq(double hertz);

    void loop();

    void pause();
    void resume();
    bool is_paused() { return controls_div_->is_paused_; }
    void handle_refresh();

    void view_clicked(const Wt::WModelIndex& index, const Wt::WMouseEvent& event);

  private:
    void handle_global_key(const Wt::WKeyEvent& event);

    void focus() override
    {
        if (last_scope_state_ == ACTIVE)
            resume();
        else if (last_scope_state_ == UNKNOWN && !is_paused())
            scope_timer_.start();

        last_scope_state_ = UNKNOWN;
    }

    void unfocus() override
    {
        if (last_scope_state_ == UNKNOWN)
        {
            last_scope_state_ = is_paused() ? STOPPED : ACTIVE;
            pause();
        }
    }

    friend class ScopeCommsThread;
    void cleanup() override
    {
        // we must resume the scope as this stops the background thread, allowing the ZeroMQService for the scope to be safely deleted. This is inelegant, but a by product of how Wt destructs the root object *after* this class (and thus all the local class objects).
        resume();
    }

    void display_notify(const std::string& value);

  private:
    const protobuf::ProtobufScopeConfig& pb_scope_config_;

    Wt::WStringListModel* history_model_;
    Wt::WStandardItemModel* model_;
    Wt::WSortFilterProxyModel* proxy_;

    Wt::WVBoxLayout* main_layout_;

    Wt::WTimer scope_timer_;
    enum ScopeState
    {
        ACTIVE = 1,
        STOPPED = 2,
        UNKNOWN = 0
    };
    ScopeState last_scope_state_;

    struct SubscriptionsContainer : Wt::WContainerWidget
    {
        SubscriptionsContainer(Wt::WStandardItemModel* model, Wt::WStringListModel* history_model,
                               std::map<std::string, int>& msg_map,
                               Wt::WContainerWidget* parent = nullptr);

        Wt::WStandardItemModel* model_;
        Wt::WStringListModel* history_model_;
        std::map<std::string, int>& msg_map_;
    };

    struct HistoryContainer : Wt::WContainerWidget
    {
        HistoryContainer(Wt::WVBoxLayout* main_layout, Wt::WAbstractItemModel* model,
                         const protobuf::ProtobufScopeConfig& pb_scope_config, LiaisonScope* scope,
                         Wt::WContainerWidget* parent);

        void handle_add_history();
        void handle_remove_history(const std::string& type);
        void add_history(const protobuf::ProtobufScopeConfig::HistoryConfig& config);
        void toggle_history_plot(Wt::WWidget* plot);
        void display_message(const std::string& group, const google::protobuf::Message& msg);
        void flush_buffer();

        void view_clicked(const Wt::WModelIndex& proxy_index, const Wt::WMouseEvent& event,
                          Wt::WStandardItemModel* model, Wt::WSortFilterProxyModel* proxy);

        struct MVC
        {
            std::string key;
            Wt::WContainerWidget* container;
            Wt::WStandardItemModel* model;
            Wt::WTreeView* tree;
            Wt::WSortFilterProxyModel* proxy;
        };

        Wt::WVBoxLayout* main_layout_;

        const protobuf::ProtobufScopeConfig& pb_scope_config_;
        std::map<std::string, MVC> history_models_;
        Wt::WText* hr_;
        Wt::WText* add_text_;
        Wt::WComboBox* history_box_;
        Wt::WPushButton* history_button_;

        boost::circular_buffer<
            std::pair<std::string, std::shared_ptr<const google::protobuf::Message>>>
            buffer_;
        LiaisonScope* scope_;
    };

    struct ControlsContainer : Wt::WContainerWidget
    {
        ControlsContainer(Wt::WTimer* timer, bool start_paused, LiaisonScope* scope,
                          SubscriptionsContainer* subscriptions_div, double freq,
                          Wt::WContainerWidget* parent = nullptr);
        ~ControlsContainer() override;

        void handle_play_pause(bool toggle_state);
        void handle_refresh();

        void pause();
        void resume();

        void increment_clicked_messages(const Wt::WMouseEvent& event);
        void decrement_clicked_messages(const Wt::WMouseEvent& event);
        void remove_clicked_message(const Wt::WMouseEvent& event);
        void clear_clicked_messages(const Wt::WMouseEvent& event);

        Wt::WTimer* timer_;

        Wt::WText* play_state_;
        Wt::WBreak* break1_;

        Wt::WPushButton* play_pause_button_;
        Wt::WPushButton* refresh_button_;

        Wt::WBreak* break2_;
        Wt::WText* freq_text_;
        Wt::WDoubleSpinBox* freq_spin_;

        bool is_paused_;
        LiaisonScope* scope_;
        SubscriptionsContainer* subscriptions_div_;

        Wt::WStackedWidget* clicked_message_stack_;
    };

    struct RegexFilterContainer : Wt::WContainerWidget
    {
        RegexFilterContainer(LiaisonScope* scope, Wt::WSortFilterProxyModel* proxy,
                             const protobuf::ProtobufScopeConfig& pb_scope_config,
                             Wt::WContainerWidget* parent = nullptr);

        void handle_set_regex_filter();
        void handle_clear_regex_filter(protobuf::ProtobufScopeConfig::Column column);

        LiaisonScope* scope_;
        Wt::WSortFilterProxyModel* proxy_;
        Wt::WText* hr_;
        Wt::WText* set_text_;

        struct RegexWidgets
        {
            Wt::WText* expression_text_;
            Wt::WLineEdit* regex_filter_text_;
            Wt::WPushButton* regex_filter_button_;
            Wt::WPushButton* regex_filter_clear_;
        };

        std::map<protobuf::ProtobufScopeConfig::Column, RegexWidgets> widgets_;
    };

    Wt::WGroupBox* main_box_;
    SubscriptionsContainer* subscriptions_div_;
    ControlsContainer* controls_div_;
    HistoryContainer* history_header_div_;
    RegexFilterContainer* regex_filter_div_;
    Wt::WTreeView* scope_tree_view_;
    WContainerWidget* bottom_fill_;

    // maps group into row
    std::map<std::string, int> msg_map_;

    std::map<std::string, std::shared_ptr<const google::protobuf::Message>> paused_buffer_;
};

class LiaisonScopeProtobufTreeView : public Wt::WTreeView
{
  public:
    LiaisonScopeProtobufTreeView(const protobuf::ProtobufScopeConfig& pb_scope_config,
                                 int scope_height, Wt::WContainerWidget* parent = nullptr);

  private:
    //           void handle_double_click(const Wt::WModelIndex& index, const Wt::WMouseEvent& event);
};

class LiaisonScopeProtobufModel : public Wt::WStandardItemModel
{
  public:
    LiaisonScopeProtobufModel(const protobuf::ProtobufScopeConfig& pb_scope_config,
                              Wt::WContainerWidget* parent = nullptr);
};

class ScopeCommsThread : public goby::zeromq::LiaisonCommsThread<LiaisonScope>
{
  public:
    ScopeCommsThread(LiaisonScope* scope, const protobuf::LiaisonConfig& config, int index)
        : LiaisonCommsThread<LiaisonScope>(scope, config, index), scope_(scope)
    {
        auto subscription_handler = [this](const std::vector<unsigned char>& data, int /*scheme*/,
                                           const std::string& type,
                                           const goby::middleware::Group& group) {
            std::string gr = group;
            try
            {
                auto pb_msg = dccl::DynamicProtobufManager::new_protobuf_message<
                    std::shared_ptr<google::protobuf::Message>>(type);
                pb_msg->ParseFromArray(&data[0], data.size());
                scope_->post_to_wt([=]() { scope_->inbox(gr, pb_msg); });
            }
            catch (const std::exception& e)
            {
                goby::glog.is_warn() && goby::glog << "Unhandled subscription: " << e.what()
                                                   << std::endl;
            }
        };

        regex_subscription_ = interprocess().subscribe_regex(
            subscription_handler, {goby::middleware::MarshallingScheme::PROTOBUF}, ".*", ".*");
    }
    ~ScopeCommsThread() override = default;

    void update_subscription(std::string group_regex, std::string type_regex)
    {
        glog.is_debug1() && glog << "Updated subscriptions with group: [" << group_regex
                                 << "], type: [" << type_regex << "]" << std::endl;
        regex_subscription_->update_group_regex(group_regex);
        regex_subscription_->update_type_regex(type_regex);
    }

  private:
    friend class LiaisonScope;
    LiaisonScope* scope_;
    std::shared_ptr<middleware::SerializationSubscriptionRegex> regex_subscription_;
};

} // namespace zeromq
} // namespace apps
} // namespace goby

#endif
