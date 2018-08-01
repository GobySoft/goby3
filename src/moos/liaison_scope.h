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

#ifndef LIAISONSCOPE20110609H
#define LIAISONSCOPE20110609H

#include <boost/thread.hpp>
#include <boost/circular_buffer.hpp>

#include <Wt/WText>
#include <Wt/WCssDecorationStyle>
#include <Wt/WBorder>
#include <Wt/WColor>
#include <Wt/WTreeView>
#include <Wt/WStandardItemModel>
#include <Wt/WEvent>
#include <Wt/WSortFilterProxyModel>
#include <Wt/WBoxLayout>
#include <Wt/WVBoxLayout>
#include <Wt/WTimer>

#include "goby/moos/moos_node.h"
#include "goby/middleware/liaison/liaison_container.h"
#include "goby/moos/protobuf/liaison_config.pb.h"

namespace Wt
{
    class WStandardItemModel;
}

namespace goby
{
    namespace common
    {
        class LiaisonScope : public LiaisonContainer, public goby::moos::MOOSNode
        {
            
          public:
            LiaisonScope(ZeroMQService* zeromq_service, const protobuf::LiaisonConfig& cfg, Wt::WContainerWidget* parent = 0);
            
            void moos_inbox(CMOOSMsg& msg);
                
            void handle_message(CMOOSMsg& msg, bool fresh_message);            
            
            static std::vector<Wt::WStandardItem *> create_row(CMOOSMsg& msg);
            static void attach_pb_rows(const std::vector<Wt::WStandardItem *>& items,
                                CMOOSMsg& msg);
            static void update_row(CMOOSMsg& msg, const std::vector<Wt::WStandardItem *>& items);
            
            void loop();
            
            void pause();
            void resume();
            bool is_paused()
            {
                return (controls_div_->paused_mail_thread_ && controls_div_->paused_mail_thread_->joinable());
            }
            
            
          private:
            void handle_global_key(Wt::WKeyEvent event);

            void focus()
            {
                if(last_scope_state_ == ACTIVE)
                    resume();
                else if(last_scope_state_ == UNKNOWN)
                    scope_timer_.start();
                
                last_scope_state_ = UNKNOWN;
            }
            
            void unfocus()
            {
                if(last_scope_state_ == UNKNOWN)
                {
                    last_scope_state_ = is_paused() ? STOPPED : ACTIVE;
                    pause();
                }
            }
            
            void cleanup()
            {
                // we must resume the scope as this stops the background thread, allowing the ZeroMQService for the scope to be safely deleted. This is inelegant, but a by product of how Wt destructs the root object *after* this class (and thus all the local class objects).
                resume();
            }
            
            
          private:
            ZeroMQService* zeromq_service_;
            const protobuf::MOOSScopeConfig& moos_scope_config_;

            Wt::WStringListModel* history_model_;
            Wt::WStandardItemModel* model_;
            Wt::WSortFilterProxyModel* proxy_;

            Wt::WVBoxLayout* main_layout_;

            Wt::WTimer scope_timer_;
            enum ScopeState { ACTIVE = 1, STOPPED = 2, UNKNOWN = 0 };
            ScopeState last_scope_state_;

            struct SubscriptionsContainer : Wt::WContainerWidget
            {
                SubscriptionsContainer(LiaisonScope* node,
                                       Wt::WStandardItemModel* model,
                                       Wt::WStringListModel* history_model,
                                       std::map<std::string, int>& msg_map,
                                       Wt::WContainerWidget* parent = 0);

                void handle_add_subscription();
                void handle_remove_subscription(Wt::WPushButton* clicked_anchor);
                void add_subscription(std::string type);
                
                void refresh_with_newest();
                void refresh_with_newest(const std::string& type);
                
                LiaisonScope* node_;
                
                Wt::WStandardItemModel* model_;
                Wt::WStringListModel* history_model_;
                std::map<std::string, int>& msg_map_;
            
                Wt::WText* add_text_;
                Wt::WLineEdit* subscribe_filter_text_;
                Wt::WPushButton* subscribe_filter_button_;
                Wt::WBreak* subscribe_break_;
                Wt::WText* remove_text_;

                std::set<std::string> subscriptions_;
            };

            SubscriptionsContainer* subscriptions_div_;
            
            struct HistoryContainer : Wt::WContainerWidget
            {
                HistoryContainer(MOOSNode* node,
                                 Wt::WVBoxLayout* main_layout,
                                 Wt::WAbstractItemModel* model,
                                 const protobuf::MOOSScopeConfig& moos_scope_config,
                                 Wt::WContainerWidget* parent = 0);

                void handle_add_history();
                void handle_remove_history(std::string type);
                void add_history(const goby::common::protobuf::MOOSScopeConfig::HistoryConfig& config);
                void toggle_history_plot(Wt::WWidget* plot);
                void display_message(CMOOSMsg& msg);
                void flush_buffer();
                
                struct MVC
                {
                    std::string key;
                    Wt::WContainerWidget* container;
                    Wt::WStandardItemModel* model;
                    Wt::WTreeView* tree;
                    Wt::WSortFilterProxyModel* proxy;
                };
                
                MOOSNode* node_;
                Wt::WVBoxLayout* main_layout_;
                
                const protobuf::MOOSScopeConfig& moos_scope_config_;
                std::map<std::string, MVC> history_models_;                
                Wt::WText* hr_;
                Wt::WText* add_text_;
                Wt::WComboBox* history_box_;
                Wt::WPushButton* history_button_;
                
                boost::circular_buffer<CMOOSMsg> buffer_;
            };
            
            HistoryContainer* history_header_div_;


            struct ControlsContainer : Wt::WContainerWidget
            {
                ControlsContainer(Wt::WTimer* timer,
                                  bool start_paused,
                                  LiaisonScope* scope,
                                  SubscriptionsContainer* subscriptions_div,
                                  HistoryContainer* history_header_div,
                                  Wt::WContainerWidget* parent = 0);
                ~ControlsContainer();
                
                void handle_play_pause(bool toggle_state);

                void pause();
                void resume();

                void run_paused_mail();
                boost::shared_ptr<boost::thread> paused_mail_thread_;
                
                Wt::WTimer* timer_;


                Wt::WPushButton* play_pause_button_;
                
                Wt::WText* spacer_;
                Wt::WText* play_state_;
                bool is_paused_;
                LiaisonScope* scope_;
                SubscriptionsContainer* subscriptions_div_;
                HistoryContainer* history_header_div_;
            };
            
            ControlsContainer* controls_div_;
            
            struct RegexFilterContainer : Wt::WContainerWidget
            {
                RegexFilterContainer(
                    Wt::WStandardItemModel* model,
                    Wt::WSortFilterProxyModel* proxy,
                    const protobuf::MOOSScopeConfig& moos_scope_config,
                    Wt::WContainerWidget* parent = 0);

                void handle_set_regex_filter();
                void handle_clear_regex_filter();
                void toggle_regex_examples_table();
                
                Wt::WStandardItemModel* model_;
                Wt::WSortFilterProxyModel* proxy_;
                
                Wt::WText* hr_;
                Wt::WText* set_text_;
                Wt::WComboBox* regex_column_select_;
                Wt::WText* expression_text_;
                Wt::WLineEdit* regex_filter_text_;
                Wt::WPushButton* regex_filter_button_;
                Wt::WPushButton* regex_filter_clear_;
                Wt::WPushButton* regex_filter_examples_;

                Wt::WBreak* break_;
                Wt::WTable* regex_examples_table_;

            };
            
            RegexFilterContainer* regex_filter_div_;


            Wt::WTreeView* scope_tree_view_;

            // maps CMOOSMsg::GetKey into row
            std::map<std::string, int> msg_map_;

            WContainerWidget* bottom_fill_;

            
            
        
        };

      class LiaisonScopeMOOSTreeView : public Wt::WTreeView
        {
          public:
            LiaisonScopeMOOSTreeView(const protobuf::MOOSScopeConfig& moos_scope_config, Wt::WContainerWidget* parent = 0);

          private:
            
            //           void handle_double_click(const Wt::WModelIndex& index, const Wt::WMouseEvent& event);

            
        };

        class LiaisonScopeMOOSModel : public Wt::WStandardItemModel
        {
          public:
            LiaisonScopeMOOSModel(const protobuf::MOOSScopeConfig& moos_scope_config, Wt::WContainerWidget* parent = 0); 
        };


    }
}

#endif
