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

#include "goby/middleware/liaison/liaison_container.h"
#include "goby/middleware/protobuf/liaison_config.pb.h"

namespace Wt
{
    class WStandardItemModel;
}

namespace goby
{
    namespace common
    {
        class ScopeCommsThread;

        class LiaisonScope : public LiaisonContainerWithComms<LiaisonScope, ScopeCommsThread>
        {
            
          public:
            LiaisonScope(const protobuf::LiaisonConfig& cfg);
            
            void inbox(const std::string& group, std::shared_ptr<const google::protobuf::Message> msg);
                
            void handle_message(const std::string& group, const google::protobuf::Message& msg, bool fresh_message);            
            
            static std::vector<Wt::WStandardItem *> create_row(const std::string& group, const google::protobuf::Message& msg);
            static void attach_pb_rows(const std::vector<Wt::WStandardItem *>& items,
                                       const google::protobuf::Message& msg);

            static void update_row(const std::string& group, const google::protobuf::Message& msg, const std::vector<Wt::WStandardItem *>& items);
            
            void loop();
            
            void pause();
            void resume();
            bool is_paused()
            {
                return controls_div_->is_paused_;
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

            friend class ScopeCommsThread;
            void cleanup()
            {
                // we must resume the scope as this stops the background thread, allowing the ZeroMQService for the scope to be safely deleted. This is inelegant, but a by product of how Wt destructs the root object *after* this class (and thus all the local class objects).
                resume();
            }
            
            
          private:
            const protobuf::ProtobufScopeConfig& pb_scope_config_;

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
                HistoryContainer(Wt::WVBoxLayout* main_layout,
                                 Wt::WAbstractItemModel* model,
                                 const protobuf::ProtobufScopeConfig& pb_scope_config,
                                 Wt::WContainerWidget* parent = 0);

                void handle_add_history();
                void handle_remove_history(std::string type);
                void add_history(const goby::common::protobuf::ProtobufScopeConfig::HistoryConfig& config);
                void toggle_history_plot(Wt::WWidget* plot);
                void display_message(const std::string& group, const google::protobuf::Message& msg);
                void flush_buffer();
                
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
                
                boost::circular_buffer<std::pair<std::string, std::shared_ptr<const google::protobuf::Message>>> buffer_;
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
                    const protobuf::ProtobufScopeConfig& pb_scope_config,
                    Wt::WContainerWidget* parent = 0);

                void handle_set_regex_filter();
                void handle_clear_regex_filter();
                
                Wt::WStandardItemModel* model_;
                Wt::WSortFilterProxyModel* proxy_;
                
                Wt::WText* hr_;
                Wt::WText* set_text_;
                Wt::WComboBox* regex_column_select_;
                Wt::WText* expression_text_;
                Wt::WLineEdit* regex_filter_text_;
                Wt::WPushButton* regex_filter_button_;
                Wt::WPushButton* regex_filter_clear_;

            };
            
            RegexFilterContainer* regex_filter_div_;


            Wt::WTreeView* scope_tree_view_;

            // maps CMOOSMsg::GetKey into row
            std::map<std::string, int> msg_map_;

            WContainerWidget* bottom_fill_;

            
            
        
        };

      class LiaisonScopeProtobufTreeView : public Wt::WTreeView
        {
          public:
            LiaisonScopeProtobufTreeView(const protobuf::ProtobufScopeConfig& pb_scope_config, Wt::WContainerWidget* parent = 0);

          private:
            
            //           void handle_double_click(const Wt::WModelIndex& index, const Wt::WMouseEvent& event);

            
        };

        class LiaisonScopeProtobufModel : public Wt::WStandardItemModel
        {
          public:
            LiaisonScopeProtobufModel(const protobuf::ProtobufScopeConfig& pb_scope_config, Wt::WContainerWidget* parent = 0); 
        };


        class ScopeCommsThread : public LiaisonCommsThread<LiaisonScope>
        {
        public:
        ScopeCommsThread(LiaisonScope* scope, const protobuf::LiaisonConfig& config, int index) :
            LiaisonCommsThread<LiaisonScope>(scope, config, index),
                scope_(scope)
            {
                auto subscription_handler = 
                    [this](const std::vector<unsigned char>& data, int scheme, const std::string& type, const Group& group) {
                        std::string gr = group;
                        try {
                            auto pb_msg = dccl::DynamicProtobufManager::new_protobuf_message<std::shared_ptr<google::protobuf::Message>>(type);
                            pb_msg->ParseFromArray(&data[0], data.size());
                            scope_->post_to_wt(
                                [=]() { scope_->inbox(gr, pb_msg); });   
                        }
                        catch(const std::exception& e)
                        {
                            goby::glog.is_warn() && goby::glog << "Unhandled subscription: " << e.what() << std::endl;
                        }
                };
                
                interprocess().subscribe_regex(subscription_handler,
                    { goby::MarshallingScheme::PROTOBUF },
                    ".*",
                    ".*");
            }
            ~ScopeCommsThread()
            {
            }
            
        private:
            friend class LiaisonScope;
            LiaisonScope* scope_;
            
        };

        
    }
}

#endif