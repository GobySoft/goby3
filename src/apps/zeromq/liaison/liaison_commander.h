// Copyright 2011-2023:
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

#ifndef GOBY_APPS_ZEROMQ_LIAISON_LIAISON_COMMANDER_H
#define GOBY_APPS_ZEROMQ_LIAISON_LIAISON_COMMANDER_H

#include <algorithm>     // for max
#include <cstdint>       // for uint64_t
#include <deque>         // for deque
#include <functional>    // for _Bind, bind
#include <map>           // for operator!=
#include <memory>        // for allocator
#include <mutex>         // for mutex
#include <set>           // for set
#include <string>        // for string
#include <unordered_map> // for unordered_...
#include <utility>       // for pair
#include <vector>        // for vector

#include <Wt/Dbo/FixedSqlConnectionPool>
#include <Wt/Dbo/Impl>
#include <Wt/Dbo/QueryModel>
#include <Wt/Dbo/Session>
#include <Wt/Dbo/SqlTraits>
#include <Wt/Dbo/WtSqlTraits>
#include <Wt/Dbo/backend/Sqlite3>
#include <Wt/WComboBox>
#include <Wt/WContainerWidget>                            // for WContainer...
#include <Wt/WDateTime>                                   // for WDateTime
#include <Wt/WEvent>                                      // for WMouseEvent
#include <Wt/WGroupBox>                                   // for WGroupBox
#include <Wt/WLength>                                     // for WLength
#include <Wt/WPushButton>                                 // for WPushButton
#include <Wt/WString>                                     // for WString
#include <Wt/WTimer>                                      // for WTimer
#include <Wt/WTreeTableNode>                              // for WTreeTable...
#include <boost/date_time/posix_time/ptime.hpp>           // for ptime
#include <boost/date_time/posix_time/time_formatters.hpp> // for to_simple_...
#include <boost/units/quantity.hpp>                       // for operator/
#include <dccl/version.h>
#include <google/protobuf/message.h> // for Message

#include "goby/middleware/group.h"                    // for Group
#include "goby/middleware/marshalling/interface.h"    // for Marshallin...
#include "goby/middleware/protobuf/intervehicle.pb.h" // for AckData
#include "goby/middleware/transport/interprocess.h"   // for InterProce...
#include "goby/middleware/transport/publisher.h"      // for Publisher
#include "goby/time/convert.h"                        // for SystemCloc...
#include "goby/time/system_clock.h"                   // for SystemClock
#include "goby/util/debug_logger/flex_ostream.h"      // for operator<<
#include "goby/zeromq/liaison/liaison_container.h"    // for LiaisonCom...
#include "goby/zeromq/protobuf/liaison_config.pb.h"   // for ProtobufCo...

namespace Wt
{
class WComboBox;
class WDialog;
class WFormWidget;
class WIconPair;
class WLabel;
class WLineEdit;
class WModelIndex;
class WPanel;
class WStackedWidget;
class WTreeTable;
class WTreeView;
class WValidator;
} // namespace Wt
namespace google
{
namespace protobuf
{
class Descriptor;
class FieldDescriptor;
} // namespace protobuf
} // namespace google

namespace goby
{
namespace apps
{
namespace zeromq
{
class LiaisonTreeTableNode : public Wt::WTreeTableNode
{
  public:
    LiaisonTreeTableNode(const Wt::WString& labelText, Wt::WIconPair* labelIcon = nullptr,
                         Wt::WTreeTableNode* parentNode = nullptr)
        : Wt::WTreeTableNode(labelText, labelIcon, parentNode)
    {
        this->labelArea()->setHeight(Wt::WLength(2.5, Wt::WLength::FontEm));
    }
};

struct ExternalData
{
    std::string affiliated_protobuf_name;
    std::string protobuf_name;
    Wt::WDateTime time;
    std::string group;
    std::string value;
    std::vector<unsigned char> bytes;

    template <class Action> void persist(Action& a)
    {
        Wt::Dbo::field(a, affiliated_protobuf_name, "affiliated_protobuf_name");
        Wt::Dbo::field(a, protobuf_name, "protobuf_name");
        Wt::Dbo::field(a, time, "time");
        Wt::Dbo::field(a, group, "group");
        Wt::Dbo::field(a, value, "value");
        Wt::Dbo::field(a, bytes, "bytes");
    }
};

struct CommandEntry
{
    std::string protobuf_name;
    std::string group;
    std::string layer;
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
        Wt::Dbo::field(a, layer, "layer");
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

class LiaisonCommander
    : public goby::zeromq::LiaisonContainerWithComms<LiaisonCommander, CommanderCommsThread>
{
  public:
    LiaisonCommander(const protobuf::LiaisonConfig& cfg);
    ~LiaisonCommander() override;
    void loop();

  private:
    void focus() override { commander_timer_.start(); }

    void unfocus() override { commander_timer_.stop(); }

    friend class CommanderCommsThread;
    void display_notify_subscription(const std::vector<unsigned char>& data, int scheme,
                                     const std::string& type, const std::string& group,
                                     const goby::apps::zeromq::protobuf::ProtobufCommanderConfig::
                                         NotificationSubscription::Color& background_color);

    void display_notify(const google::protobuf::Message& pb_msg, const std::string& title,
                        const goby::apps::zeromq::protobuf::ProtobufCommanderConfig::
                            NotificationSubscription::Color& background_color);

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
        void clear_incoming_messages(const Wt::WMouseEvent& event);

        struct CommandContainer : Wt::WGroupBox
        {
            CommandContainer(const protobuf::ProtobufCommanderConfig& pb_commander_config,
                             const protobuf::ProtobufCommanderConfig::LoadProtobuf& load_config,
                             const std::string& protobuf_name, Wt::Dbo::Session* session,
                             LiaisonCommander* commander, Wt::WPushButton* send_button);

            void generate_root();

            void generate_tree(Wt::WTreeTableNode* parent, google::protobuf::Message* message,
                               const std::string& parent_hierarchy = "", int index = -1);
            void generate_tree_row(Wt::WTreeTableNode* parent, google::protobuf::Message* message,
                                   const google::protobuf::FieldDescriptor* field_desc,
                                   const std::string& parent_hierarchy = "");

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

            void set_external_data_model_params(
                Wt::Dbo::QueryModel<Wt::Dbo::ptr<ExternalData>>* external_data_model);
            void set_external_data_table_params(Wt::WTreeView* external_data_table);
            void load_groups(const google::protobuf::Descriptor* desc);
            void load_external_data(const google::protobuf::Descriptor* desc);

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
                                              Wt::WPushButton* field, Wt::WTreeTableNode* parent,
                                              const std::string& parent_hierarchy);

            void handle_load_external_data(const Wt::WMouseEvent& mouse,
                                           google::protobuf::Message* message,
                                           const google::protobuf::FieldDescriptor* field_desc,
                                           Wt::WPushButton* field, Wt::WTreeTableNode* parent,
                                           const std::string& parent_hierarchy);

            std::pair<const google::protobuf::FieldDescriptor*,
                      std::vector<google::protobuf::Message*>>
            find_fully_qualified_field(std::vector<google::protobuf::Message*> msgs,
                                       std::deque<std::string> fields, bool set_field = false,
                                       int set_index = 0);

            void handle_line_field_changed(google::protobuf::Message* message,
                                           const google::protobuf::FieldDescriptor* field_desc,
                                           Wt::WLineEdit* field, int index);

            void handle_focus_changed(Wt::WLineEdit* field);

            void handle_combo_field_changed(google::protobuf::Message* message,
                                            const google::protobuf::FieldDescriptor* field_desc,
                                            Wt::WComboBox* field, int index);

            void handle_repeated_size_change(int size, google::protobuf::Message* message,
                                             const google::protobuf::FieldDescriptor* field_desc,
                                             Wt::WTreeTableNode* parent,
                                             const std::string& parent_hierarchy);

            void handle_database_double_click(const Wt::WModelIndex& index,
                                              const Wt::WMouseEvent& event);

            void check_initialized()
            {
                if (!message_ || !message_->IsInitialized())
                    send_button_->setDisabled(true);
                else
                    send_button_->setDisabled(false);
            }

            void check_dynamics()
            {
#if DCCL_VERSION_MAJOR >= 4
                if (has_dynamic_conditions_ && !skip_dynamic_conditions_update_ // avoid recursion
                )
                    generate_root();
#endif
            }

            void update_oneofs(google::protobuf::Message* message,
                               const google::protobuf::FieldDescriptor* field_desc,
                               Wt::WFormWidget* changed_field);

            enum DatabaseDialogResponse
            {
                RESPONSE_EDIT,
                RESPONSE_MERGE,
                RESPONSE_CANCEL
            };

            void handle_database_dialog(DatabaseDialogResponse response,
                                        const std::shared_ptr<google::protobuf::Message>& message,
                                        const std::string& group, const std::string& layer);

            void handle_external_data(std::string type, std::string group,
                                      const std::shared_ptr<const google::protobuf::Message>& msg);

            std::string create_field_hierarchy(const google::protobuf::FieldDescriptor* field_desc);

            std::shared_ptr<google::protobuf::Message> message_;

            std::map<Wt::WFormWidget*, const google::protobuf::FieldDescriptor*> time_fields_;
            std::uint64_t latest_time_;

            Wt::WContainerWidget* group_div_;
            Wt::WLabel* group_label_;
            Wt::WComboBox* group_selection_;

            std::vector<
                goby::apps::zeromq::protobuf::ProtobufCommanderConfig::LoadProtobuf::GroupLayer>
                publish_to_;

            struct ExternalDataMeta
            {
                goby::apps::zeromq::protobuf::ProtobufCommanderConfig::LoadProtobuf::ExternalData
                    pb;
                const google::protobuf::Descriptor* external_desc{nullptr};
            };

            // maps field hierarchy to external_desc to ExternalData metadata
            std::map<std::string, std::map<std::string, ExternalDataMeta>>
                externally_loadable_fields_;

            std::set<const google::protobuf::Descriptor*> external_types_;

            Wt::WGroupBox* message_tree_box_;
            Wt::WTreeTable* message_tree_table_;

            //                    Wt::WStackedWidget* field_info_stack_;
            //                    std::map<const google::protobuf::FieldDescriptor*, int> field_info_map_;

            Wt::Dbo::Session* session_;
            Wt::Dbo::QueryModel<Wt::Dbo::ptr<CommandEntry>>* sent_model_;
            Wt::WGroupBox* sent_box_;
            Wt::WPushButton* sent_clear_;
            Wt::WTreeView* sent_table_;

            Wt::Dbo::QueryModel<Wt::Dbo::ptr<ExternalData>>* external_data_model_;
            Wt::WGroupBox* external_data_box_;
            Wt::WPushButton* external_data_clear_;
            Wt::WTreeView* external_data_table_;

            boost::posix_time::ptime last_reload_time_;

            std::shared_ptr<Wt::WDialog> database_dialog_;

            const protobuf::ProtobufCommanderConfig& pb_commander_config_;
            const protobuf::ProtobufCommanderConfig::LoadProtobuf& load_config_;
            LiaisonCommander* commander_;
            Wt::WPushButton* send_button_;

#if DCCL_VERSION_MAJOR >= 4
            // does any field in the message have dynamic conditions set?
            bool has_dynamic_conditions_{false};
            dccl::DynamicConditions dccl_dycon_;
#endif
            bool skip_dynamic_conditions_update_{true};

            std::map<const google::protobuf::Message*, std::map<const google::protobuf::OneofDescriptor*,
                                                          std::vector<Wt::WFormWidget*>>>
                oneof_fields_;
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
    static Wt::Dbo::backend::Sqlite3* sqlite3_;
    static std::unique_ptr<Wt::Dbo::FixedSqlConnectionPool> connection_pool_;
};

class CommanderCommsThread : public goby::zeromq::LiaisonCommsThread<LiaisonCommander>
{
  public:
    CommanderCommsThread(LiaisonCommander* commander, const protobuf::LiaisonConfig& config,
                         int index)
        : LiaisonCommsThread<LiaisonCommander>(commander, config, index), commander_(commander)
    {
        {
            using std::placeholders::_1;
            using std::placeholders::_2;
            command_publisher_ = {
                {},
                std::bind(&CommanderCommsThread::set_command_group, this, _1, _2),
                std::bind(&CommanderCommsThread::handle_command_ack, this, _1, _2),
                std::bind(&CommanderCommsThread::handle_command_expired, this, _1, _2)};
        }

        for (const auto& notify : cfg().pb_commander_config().notify_subscribe())
        {
            interprocess().subscribe_regex(
                [this, notify](const std::vector<unsigned char>& data, int scheme,
                               const std::string& type, const goby::middleware::Group& group) {
                    std::string gr = group;
                    commander_->post_to_wt([=]() {
                        auto background_color = notify.background_color();
                        if (!notify.has_background_color())
                        {
                            background_color.set_r(255);
                            background_color.set_g(255);
                            background_color.set_b(255);
                        }

                        commander_->display_notify_subscription(data, scheme, type, gr,
                                                                background_color);
                    });
                },
                {goby::middleware::MarshallingScheme::PROTOBUF}, notify.type_regex(),
                notify.group_regex());
        }
    }
    ~CommanderCommsThread() override = default;

  private:
    void set_command_group(google::protobuf::Message& command, const goby::middleware::Group& group)
    {
    }

    void handle_command_ack(const google::protobuf::Message& command,
                            const goby::middleware::intervehicle::protobuf::AckData& ack)
    {
        goby::apps::zeromq::protobuf::ProtobufCommanderConfig::NotificationSubscription::Color
            bg_color;
        bg_color.set_r(100);
        bg_color.set_g(200);
        bg_color.set_b(100);

        std::shared_ptr<google::protobuf::Message> pb_msg(command.New());
        pb_msg->CopyFrom(command);
        std::string title = std::string("Ack: ") + ack.ShortDebugString() + " @ " +
                            boost::posix_time::to_simple_string(
                                goby::time::SystemClock::now<boost::posix_time::ptime>());
        commander_->post_to_wt([=]() { commander_->display_notify(*pb_msg, title, bg_color); });
    }

    void handle_command_expired(const google::protobuf::Message& command,
                                const goby::middleware::intervehicle::protobuf::ExpireData& expire)
    {
        goby::apps::zeromq::protobuf::ProtobufCommanderConfig::NotificationSubscription::Color
            bg_color;
        bg_color.set_r(200);
        bg_color.set_g(100);
        bg_color.set_b(100);

        std::shared_ptr<google::protobuf::Message> pb_msg(command.New());
        pb_msg->CopyFrom(command);
        std::string title = std::string("Expire: ") + expire.ShortDebugString() + " @ " +
                            boost::posix_time::to_simple_string(
                                goby::time::SystemClock::now<boost::posix_time::ptime>());
        commander_->post_to_wt([=]() { commander_->display_notify(*pb_msg, title, bg_color); });
    };

  private:
    friend class LiaisonCommander;
    LiaisonCommander* commander_;
    goby::middleware::Publisher<google::protobuf::Message> command_publisher_;
};

} // namespace zeromq
} // namespace apps
} // namespace goby

#endif
