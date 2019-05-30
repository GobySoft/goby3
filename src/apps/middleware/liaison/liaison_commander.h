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

#ifndef LIAISONCOMMANDER20110609H
#define LIAISONCOMMANDER20110609H

#include <Wt/WAbstractListModel>
#include <Wt/WBorder>
#include <Wt/WBreak>
#include <Wt/WColor>
#include <Wt/WComboBox>
#include <Wt/WCssDecorationStyle>
#include <Wt/WDateTime>
#include <Wt/WDialog>
#include <Wt/WGroupBox>
#include <Wt/WLabel>
#include <Wt/WLineEdit>
#include <Wt/WPushButton>
#include <Wt/WSpinBox>
#include <Wt/WStackedWidget>
#include <Wt/WString>
#include <Wt/WStringListModel>
#include <Wt/WTableCell>
#include <Wt/WTableView>
#include <Wt/WText>
#include <Wt/WTimer>
#include <Wt/WTreeTable>
#include <Wt/WTreeTableNode>
#include <Wt/WTreeView>
#include <Wt/WVBoxLayout>
#include <Wt/WValidator>

#include <Wt/Dbo/FixedSqlConnectionPool>
#include <Wt/Dbo/Impl>
#include <Wt/Dbo/QueryModel>
#include <Wt/Dbo/Session>
#include <Wt/Dbo/SqlTraits>
#include <Wt/Dbo/WtSqlTraits>
#include <Wt/Dbo/backend/Sqlite3>

#include "goby/middleware/liaison/liaison_container.h"
#include "goby/middleware/multi-thread-application.h"
#include "goby/middleware/protobuf/liaison_config.pb.h"
#include "goby/time.h"
#include "goby/util/as.h"
#include "goby/util/debug_logger.h"

#include "liaison.h"

namespace goby
{
namespace common
{
class LiaisonTreeTableNode : public Wt::WTreeTableNode
{
  public:
    LiaisonTreeTableNode(const Wt::WString& labelText, Wt::WIconPair* labelIcon = 0,
                         Wt::WTreeTableNode* parentNode = 0)
        : Wt::WTreeTableNode(labelText, labelIcon, parentNode)
    {
        this->labelArea()->setHeight(Wt::WLength(2.5, Wt::WLength::FontEm));
    }
};

struct CommandEntry
{
    std::string protobuf_name;
    std::string group;
    std::vector<unsigned char> bytes;
    long long utime;
    Wt::WDateTime time;
    std::string comment;
    std::string address;
    int last_ack;
    // serialized NetworkAckSet
    std::vector<unsigned char> acks;

    template <class Action> void persist(Action& a)
    {
        Wt::Dbo::field(a, protobuf_name, "protobuf_name");
        Wt::Dbo::field(a, group, "group");
        Wt::Dbo::field(a, bytes, "bytes");
        Wt::Dbo::field(a, utime, "utime");
        Wt::Dbo::field(a, time, "time");
        Wt::Dbo::field(a, comment, "comment");
        Wt::Dbo::field(a, address, "address");
        Wt::Dbo::field(a, last_ack, "last_ack");
        Wt::Dbo::field(a, acks, "acks");
    }
};

class CommanderCommsThread;

class LiaisonCommander : public LiaisonContainerWithComms<LiaisonCommander, CommanderCommsThread>
{
  public:
    LiaisonCommander(const protobuf::LiaisonConfig& cfg);
    void loop();

  private:
    void focus() { commander_timer_.start(); }

    void unfocus() { commander_timer_.stop(); }

    friend class CommanderCommsThread;
    void display_notify_subscription(const std::vector<unsigned char>& data, int scheme,
                                     const std::string& type, const std::string& group);

  private:
    const protobuf::ProtobufCommanderConfig& pb_commander_config_;
    std::set<std::string> display_subscriptions_;

    struct ControlsContainer : Wt::WGroupBox
    {
        ControlsContainer(const protobuf::ProtobufCommanderConfig& pb_commander_config,
                          Wt::WStackedWidget* commands_div, LiaisonCommander* parent);
        void switch_command(int selection_index);

        void clear_message();
        void send_message();

        void increment_incoming_messages(const Wt::WMouseEvent& event);
        void decrement_incoming_messages(const Wt::WMouseEvent& event);
        void remove_incoming_message(const Wt::WMouseEvent& event);

        struct CommandContainer : Wt::WGroupBox
        {
            CommandContainer(const protobuf::ProtobufCommanderConfig& pb_commander_config,
                             const std::string& protobuf_name, Wt::Dbo::Session* session);

            //                        Wt::WStackedWidget* master_field_info_stack);

            void generate_root();

            void generate_tree(Wt::WTreeTableNode* parent, google::protobuf::Message* message);
            void generate_tree_row(Wt::WTreeTableNode* parent, google::protobuf::Message* message,
                                   const google::protobuf::FieldDescriptor* field_desc);

            void generate_tree_field(Wt::WFormWidget*& value_field,
                                     google::protobuf::Message* message,
                                     const google::protobuf::FieldDescriptor* field_desc,
                                     int index = -1);

            Wt::WLineEdit*
            generate_single_line_edit_field(google::protobuf::Message* message,
                                            const google::protobuf::FieldDescriptor* field_desc,
                                            const std::string& current_value,
                                            const std::string& default_value,
                                            Wt::WValidator* validator, int index = -1);

            Wt::WComboBox*
            generate_combo_box_field(google::protobuf::Message* message,
                                     const google::protobuf::FieldDescriptor* field_desc,
                                     const std::vector<Wt::WString>& strings, int current_value,
                                     const std::string& default_value, int index = -1);

            void generate_field_info_box(Wt::WFormWidget*& value_field,
                                         const google::protobuf::FieldDescriptor* field_desc);

            void set_time_field(Wt::WFormWidget* value_field,
                                const google::protobuf::FieldDescriptor* field_desc);

            /* void queue_default_value_field( */
            /*     Wt::WFormWidget*& value_field, */
            /*     const google::protobuf::FieldDescriptor* field_desc); */

            void dccl_default_value_field(Wt::WFormWidget*& value_field,
                                          const google::protobuf::FieldDescriptor* field_desc);

            void dccl_default_modify_field(Wt::WFormWidget*& modify_field,
                                           const google::protobuf::FieldDescriptor* field_desc);

            std::string
            string_from_dccl_double(double* value,
                                    const google::protobuf::FieldDescriptor* field_desc);

            //                    void handle_field_focus(int field_info_index);

            void handle_toggle_single_message(const Wt::WMouseEvent& mouse,
                                              google::protobuf::Message* message,
                                              const google::protobuf::FieldDescriptor* field_desc,
                                              Wt::WPushButton* field, Wt::WTreeTableNode* parent);

            void handle_line_field_changed(google::protobuf::Message* message,
                                           const google::protobuf::FieldDescriptor* field_desc,
                                           Wt::WLineEdit* field, int index);

            void handle_combo_field_changed(google::protobuf::Message* message,
                                            const google::protobuf::FieldDescriptor* field_desc,
                                            Wt::WComboBox* field, int index);

            void handle_repeated_size_change(int size, google::protobuf::Message* message,
                                             const google::protobuf::FieldDescriptor* field_desc,
                                             Wt::WTreeTableNode* parent);

            void handle_database_double_click(const Wt::WModelIndex& index,
                                              const Wt::WMouseEvent& event);

            enum DatabaseDialogResponse
            {
                RESPONSE_EDIT,
                RESPONSE_MERGE,
                RESPONSE_CANCEL
            };

            void handle_database_dialog(DatabaseDialogResponse response,
                                        std::shared_ptr<google::protobuf::Message> message,
                                        std::string group);

            std::shared_ptr<google::protobuf::Message> message_;

            std::map<Wt::WFormWidget*, const google::protobuf::FieldDescriptor*> time_fields_;
            std::uint64_t latest_time_;

            Wt::WContainerWidget* group_div_;
            Wt::WLabel* group_label_;
            Wt::WLineEdit* group_line_;

            Wt::WGroupBox* tree_box_;
            Wt::WTreeTable* tree_table_;

            //                    Wt::WStackedWidget* field_info_stack_;
            //                    std::map<const google::protobuf::FieldDescriptor*, int> field_info_map_;

            Wt::Dbo::Session* session_;
            Wt::Dbo::QueryModel<Wt::Dbo::ptr<CommandEntry> >* query_model_;

            Wt::WGroupBox* query_box_;
            Wt::WTreeView* query_table_;

            boost::posix_time::ptime last_reload_time_;

            std::shared_ptr<Wt::WDialog> database_dialog_;

            const protobuf::ProtobufCommanderConfig& pb_commander_config_;
        };

        const protobuf::ProtobufCommanderConfig& pb_commander_config_;
        std::map<std::string, int> commands_;

        Wt::WContainerWidget* command_div_;
        Wt::WLabel* command_label_;
        Wt::WComboBox* command_selection_;

        Wt::WContainerWidget* buttons_div_;
        Wt::WLabel* comment_label_;
        Wt::WLineEdit* comment_line_;
        Wt::WPushButton* send_button_;
        Wt::WPushButton* clear_button_;
        Wt::WStackedWidget* commands_div_;

        Wt::WPanel* incoming_message_panel_;
        Wt::WStackedWidget* incoming_message_stack_;

        //                Wt::WPanel* master_field_info_panel_;
        //               Wt::WStackedWidget* master_field_info_stack_;

        Wt::Dbo::Session session_;
        LiaisonCommander* commander_;
    };

    Wt::WStackedWidget* commands_div_;
    ControlsContainer* controls_div_;

    Wt::WTimer commander_timer_;

    // static database objects
    static boost::posix_time::ptime last_db_update_time_;
    static std::mutex dbo_mutex_;
    static std::shared_ptr<Wt::Dbo::backend::Sqlite3> sqlite3_;
    static std::shared_ptr<Wt::Dbo::FixedSqlConnectionPool> connection_pool_;
};

class CommanderCommsThread : public LiaisonCommsThread<LiaisonCommander>
{
  public:
    CommanderCommsThread(LiaisonCommander* commander, const protobuf::LiaisonConfig& config,
                         int index)
        : LiaisonCommsThread<LiaisonCommander>(commander, config, index), commander_(commander)
    {
        for (const auto& notify : cfg().pb_commander_config().notify_subscribe())
        {
            interprocess().subscribe_regex(
                [this](const std::vector<unsigned char>& data, int scheme, const std::string& type,
                       const Group& group) {
                    std::string gr = group;
                    commander_->post_to_wt(
                        [=]() { commander_->display_notify_subscription(data, scheme, type, gr); });
                },
                {goby::MarshallingScheme::PROTOBUF}, notify.type_regex(), notify.group_regex());
        }
    }
    ~CommanderCommsThread() {}

  private:
    friend class LiaisonCommander;
    LiaisonCommander* commander_;
};

} // namespace common
} // namespace goby

#endif
