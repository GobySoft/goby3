// Copyright 2019-2020:
//   GobySoft, LLC (2013-)
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

#include <Wt/Dbo/Exception>
#include <Wt/WApplication>
#include <Wt/WDoubleValidator>
#include <Wt/WEnvironment>
#include <Wt/WIntValidator>
#include <Wt/WLengthValidator>
#include <Wt/WPanel>
#include <Wt/WRegExpValidator>
#include <cfloat>
#include <cmath>

#include "dccl/common.h"
#include "dccl/dynamic_protobuf_manager.h"
#include "dccl/protobuf/option_extensions.pb.h"
#include "goby/acomms/protobuf/network_ack.pb.h"
#include "goby/middleware/common.h"
#include "goby/util/binary.h"
#include "goby/util/sci.h"

#include "liaison_commander.h"

using namespace Wt;
using namespace goby::util::logger;
using goby::glog;
using goby::util::logger_lock::lock;

std::mutex goby::apps::zeromq::LiaisonCommander::dbo_mutex_;
Dbo::backend::Sqlite3* goby::apps::zeromq::LiaisonCommander::sqlite3_(nullptr);
std::unique_ptr<Dbo::FixedSqlConnectionPool> goby::apps::zeromq::LiaisonCommander::connection_pool_;
boost::posix_time::ptime goby::apps::zeromq::LiaisonCommander::last_db_update_time_(
    goby::time::SystemClock::now<boost::posix_time::ptime>());

const std::string MESSAGE_INCLUDE_TEXT = "include";
const std::string MESSAGE_REMOVE_TEXT = "remove";

const std::string STRIPE_ODD_CLASS = "odd";
const std::string STRIPE_EVEN_CLASS = "even";

goby::apps::zeromq::protobuf::ProtobufCommanderConfig::LoadProtobuf::GroupLayer
to_group_layer(std::string group, std::string layer)
{
    goby::apps::zeromq::protobuf::ProtobufCommanderConfig::LoadProtobuf::GroupLayer grouplayer;
    grouplayer.set_group(group);
    goby::middleware::protobuf::Layer layer_enum;
    if (goby::middleware::protobuf::Layer_Parse(layer, &layer_enum))
        grouplayer.set_layer(layer_enum);
    return grouplayer;
}

std::string
to_string(const goby::apps::zeromq::protobuf::ProtobufCommanderConfig::LoadProtobuf::GroupLayer&
              grouplayer,
          std::uint8_t groupnum = goby::middleware::Group::invalid_numeric_group)
{
    std::string groupnum_str;

    if (grouplayer.layer() >= goby::middleware::protobuf::LAYER_INTERVEHICLE)
    {
        if (groupnum == goby::middleware::Group::invalid_numeric_group &&
            grouplayer.has_group_numeric_field_name())
            groupnum_str =
                std::string("/{value of \"" + grouplayer.group_numeric_field_name() + "\"}");
        else
            groupnum_str = std::string("/") + std::to_string(groupnum);
    }

    return grouplayer.group() + groupnum_str + " [" +
           goby::middleware::to_string(grouplayer.layer()) + "]";
}

goby::apps::zeromq::LiaisonCommander::LiaisonCommander(const protobuf::LiaisonConfig& cfg)
    : LiaisonContainerWithComms<LiaisonCommander, CommanderCommsThread>(cfg),
      pb_commander_config_(cfg.pb_commander_config()),
      commands_div_(new WStackedWidget),
      controls_div_(new ControlsContainer(pb_commander_config_, commands_div_, this))
{
    addWidget(commands_div_);

    commander_timer_.setInterval(1 / cfg.update_freq() * 1.0e3);
    commander_timer_.timeout().connect(this, &LiaisonCommander::loop);

    set_name("Commander");
}

goby::apps::zeromq::LiaisonCommander::~LiaisonCommander() {}

void goby::apps::zeromq::LiaisonCommander::display_notify_subscription(
    const std::vector<unsigned char>& data, int scheme, const std::string& type,
    const std::string& group,
    const goby::apps::zeromq::protobuf::ProtobufCommanderConfig::NotificationSubscription::Color&
        background_color)
{
    goby::glog.is_debug1() && goby::glog << "wt group: " << group << std::endl;

    try
    {
        auto pb_msg = dccl::DynamicProtobufManager::new_protobuf_message<
            std::unique_ptr<google::protobuf::Message>>(type);
        pb_msg->ParseFromArray(&data[0], data.size());

        glog.is(DEBUG1) && glog << "Received notify msg: " << pb_msg->ShortDebugString()
                                << std::endl;

        std::string title = type + "/" + group + " @ " +
                            boost::posix_time::to_simple_string(
                                goby::time::SystemClock::now<boost::posix_time::ptime>());

        display_notify(*pb_msg, title, background_color);
    }
    catch (const std::exception& e)
    {
        glog.is(WARN) && glog << "Unhandled notify subscription: " << e.what() << std::endl;
    }
}

void goby::apps::zeromq::LiaisonCommander::display_notify(
    const google::protobuf::Message& pb_msg, std::string title,
    const goby::apps::zeromq::protobuf::ProtobufCommanderConfig::NotificationSubscription::Color&
        background_color)
{
    WContainerWidget* new_div = new WContainerWidget(controls_div_->incoming_message_stack_);

    new WText("Message: " + goby::util::as<std::string>(
                                controls_div_->incoming_message_stack_->children().size()),
              new_div);

    WGroupBox* box = new WGroupBox(title, new_div);

    new_div->decorationStyle().setBackgroundColor(Wt::WColor(
        background_color.r(), background_color.g(), background_color.b(), background_color.a()));

    new WText("<pre>" + pb_msg.DebugString() + "</pre>", box);

    WPushButton* minus = new WPushButton("-", new_div);
    WPushButton* plus = new WPushButton("+", new_div);

    WPushButton* remove = new WPushButton("x", new_div);
    remove->setFloatSide(Wt::Right);

    plus->clicked().connect(controls_div_, &ControlsContainer::increment_incoming_messages);
    minus->clicked().connect(controls_div_, &ControlsContainer::decrement_incoming_messages);
    remove->clicked().connect(controls_div_, &ControlsContainer::remove_incoming_message);
    controls_div_->incoming_message_stack_->setCurrentIndex(
        controls_div_->incoming_message_stack_->children().size() - 1);
}

void goby::apps::zeromq::LiaisonCommander::ControlsContainer::increment_incoming_messages(
    const WMouseEvent& event)
{
    int new_index = incoming_message_stack_->currentIndex() + 1;
    if (new_index == static_cast<int>(incoming_message_stack_->children().size()))
        new_index = 0;

    incoming_message_stack_->setCurrentIndex(new_index);
}

void goby::apps::zeromq::LiaisonCommander::ControlsContainer::decrement_incoming_messages(
    const WMouseEvent& event)
{
    int new_index = static_cast<int>(incoming_message_stack_->currentIndex()) - 1;
    if (new_index < 0)
        new_index = incoming_message_stack_->children().size() - 1;

    incoming_message_stack_->setCurrentIndex(new_index);
}

void goby::apps::zeromq::LiaisonCommander::ControlsContainer::remove_incoming_message(
    const WMouseEvent& event)
{
    WWidget* remove = incoming_message_stack_->currentWidget();
    decrement_incoming_messages(event);
    incoming_message_stack_->removeWidget(remove);
}

void goby::apps::zeromq::LiaisonCommander::loop()
{
    ControlsContainer::CommandContainer* current_command =
        dynamic_cast<ControlsContainer::CommandContainer*>(
            controls_div_->commands_div_->currentWidget());

    if (current_command && current_command->time_fields_.size())
    {
        for (std::map<Wt::WFormWidget*, const google::protobuf::FieldDescriptor*>::iterator
                 it = current_command->time_fields_.begin(),
                 end = current_command->time_fields_.end();
             it != end; ++it)
            current_command->set_time_field(it->first, it->second);
    }

    if (current_command && (last_db_update_time_ > current_command->last_reload_time_))
    {
        glog.is(DEBUG1) && glog << "Reloading command!" << std::endl;
        glog.is(DEBUG1) && glog << last_db_update_time_ << "/" << current_command->last_reload_time_
                                << std::endl;

        std::lock_guard<std::mutex> slock(dbo_mutex_);
        Dbo::Transaction transaction(controls_div_->session_);
        current_command->query_model_->reload();
        current_command->last_reload_time_ =
            goby::time::SystemClock::now<boost::posix_time::ptime>();
    }
}

goby::apps::zeromq::LiaisonCommander::ControlsContainer::ControlsContainer(
    const protobuf::ProtobufCommanderConfig& pb_commander_config, WStackedWidget* commands_div,
    LiaisonCommander* parent)
    : WGroupBox("Controls", parent),
      pb_commander_config_(pb_commander_config),
      command_div_(new WContainerWidget(this)),
      command_label_(new WLabel("Message: ", command_div_)),
      command_selection_(new WComboBox(command_div_)),
      buttons_div_(new WContainerWidget(this)),
      comment_label_(new WLabel("Log comment: ", buttons_div_)),
      comment_line_(new WLineEdit(buttons_div_)),
      send_button_(new WPushButton("Send", buttons_div_)),
      clear_button_(new WPushButton("Clear", buttons_div_)),
      commands_div_(commands_div),
      //      incoming_message_panel_(new Wt::WPanel(this)),
      incoming_message_stack_(new Wt::WStackedWidget(this)),
      //      master_field_info_panel_(new Wt::WPanel(this)),
      //      master_field_info_stack_(new Wt::WStackedWidget(this)),
      commander_(parent)
{
    // if we're the first thread, make the database connection
    if (!sqlite3_)
    {
        std::lock_guard<std::mutex> slock(dbo_mutex_);
        sqlite3_ = new Dbo::backend::Sqlite3(pb_commander_config_.sqlite3_database());

        // connection_pool takes ownership of sqlite3_ pointer
        connection_pool_.reset(
            new Dbo::FixedSqlConnectionPool(sqlite3_, pb_commander_config_.database_pool_size()));
    }

    {
        std::lock_guard<std::mutex> slock(dbo_mutex_);
        session_.setConnectionPool(*connection_pool_);
        session_.mapClass<CommandEntry>("_liaison_commands");

        try
        {
            session_.createTables();
        }
        catch (Dbo::Exception& e)
        {
            glog.is(VERBOSE) && glog << "Could not create tables: " << e.what() << std::endl;
        }
    }

    //    incoming_message_panel_->setPositionScheme(Wt::Fixed);
    //    incoming_message_panel_->setOffsets(20, Wt::Left | Wt::Bottom);

    //    incoming_message_panel_->setTitle("Incoming messages");
    //   incoming_message_panel_->setCollapsible(true);
    //    incoming_message_panel_->setCentralWidget(incoming_message_stack_);
    //    incoming_message_panel_->addStyleClass("fixed-left");
    incoming_message_stack_->addStyleClass("fixed-left");

    //    Wt::WCssDecorationStyle field_info_style;
    //    field_info_style.setBackgroundColor(WColor(white));

    //    master_field_info_panel_->setTitle("Field Information");
    //    master_field_info_panel_->setCollapsible(true);
    //    master_field_info_panel_->setCentralWidget(master_field_info_stack_);

    //    master_field_info_panel_->setDecorationStyle(field_info_style);

    //    master_field_info_panel_->setPositionScheme(Wt::Fixed);
    //    master_field_info_panel_->setOffsets(20, Wt::Right | Wt::Bottom);
    //    master_field_info_panel_->setStyleClass("fixed-right");
    //    master_field_info_stack_->setStyleClass("fixed-right");

    send_button_->setDisabled(true);
    clear_button_->setDisabled(true);
    comment_line_->setDisabled(true);

    comment_label_->setBuddy(comment_line_);

    command_selection_->addItem("(Select a command message)");
    send_button_->clicked().connect(this, &ControlsContainer::send_message);
    clear_button_->clicked().connect(this, &ControlsContainer::clear_message);

    Dbo::ptr<CommandEntry> last_command(static_cast<CommandEntry*>(0));
    {
        std::lock_guard<std::mutex> slock(dbo_mutex_);
        Dbo::Transaction transaction(session_);
        last_command = session_.find<CommandEntry>("ORDER BY time DESC LIMIT 1");
        if (last_command)
            glog.is(DEBUG1) && glog << "Last command was of type: " << last_command->protobuf_name
                                    << std::endl;
    }

    command_selection_->activated().connect(this, &ControlsContainer::switch_command);

    for (int i = 0, n = pb_commander_config.load_protobuf_size(); i < n; ++i)
    {
        const auto& protobuf_name = pb_commander_config.load_protobuf(i).name();
        const google::protobuf::Descriptor* desc =
            dccl::DynamicProtobufManager::find_descriptor(protobuf_name);

        if (!desc)
        {
            glog.is(WARN) &&
                glog << "Could not find protobuf name " << protobuf_name
                     << " to load for Protobuf Commander (configuration line `load_protobuf_name`)"
                     << std::endl;
        }
        else
        {
            command_selection_->addItem(protobuf_name);

            if (!commands_.count(protobuf_name))
            {
                CommandContainer* new_command =
                    new CommandContainer(pb_commander_config_, protobuf_name, &session_);

                //master_field_info_stack_);
                commands_div_->addWidget(new_command);
                // index of the newly added widget
                commands_[protobuf_name] = commands_div_->count() - 1;

                for (const auto& grouplayer : pb_commander_config.load_protobuf(i).publish_to())
                {
                    // validate grouplayer
                    bool grouplayer_valid = true;

                    if (grouplayer.has_group_numeric_field_name())
                    {
                        auto group_numeric_field =
                            desc->FindFieldByName(grouplayer.group_numeric_field_name());

                        if (!group_numeric_field)
                        {
                            glog.is(WARN) && glog << "In message " << protobuf_name
                                                  << ": could not find field named "
                                                  << grouplayer.group_numeric_field_name()
                                                  << " to use for group numeric value" << std::endl;
                            grouplayer_valid = false;
                        }
                        else if (group_numeric_field->cpp_type() !=
                                     google::protobuf::FieldDescriptor::CPPTYPE_INT32 &&
                                 group_numeric_field->cpp_type() !=
                                     google::protobuf::FieldDescriptor::CPPTYPE_INT64 &&
                                 group_numeric_field->cpp_type() !=
                                     google::protobuf::FieldDescriptor::CPPTYPE_UINT32 &&
                                 group_numeric_field->cpp_type() !=
                                     google::protobuf::FieldDescriptor::CPPTYPE_UINT64 &&
                                 group_numeric_field->cpp_type() !=
                                     google::protobuf::FieldDescriptor::CPPTYPE_ENUM)
                        {
                            glog.is(WARN) && glog << "In message " << protobuf_name
                                                  << ": field named "
                                                  << grouplayer.group_numeric_field_name()
                                                  << " must be (u)int(32|64) or enum type to use "
                                                     "for group numeric value"
                                                  << std::endl;
                            grouplayer_valid = false;
                        }
                    }

                    if (grouplayer_valid)
                    {
                        new_command->group_selection_->addItem(to_string(grouplayer));
                        new_command->publish_to_.push_back(grouplayer);
                    }
                }
            }
        }
    }

    command_selection_->model()->sort(0);

    if (last_command)
    {
        int last_command_index = command_selection_->findText(last_command->protobuf_name);
        if (last_command_index >= 0)
        {
            command_selection_->setCurrentIndex(last_command_index);
            switch_command(command_selection_->currentIndex());
        }
    }
    else
    {
        switch_command(0);
    }
}

void goby::apps::zeromq::LiaisonCommander::ControlsContainer::switch_command(int selection_index)
{
    if (selection_index == 0)
    {
        send_button_->setDisabled(true);
        clear_button_->setDisabled(true);
        comment_line_->setDisabled(true);
        commands_div_->hide();
        return;
    }

    commands_div_->show();
    send_button_->setDisabled(false);
    clear_button_->setDisabled(false);
    comment_line_->setDisabled(false);

    std::string protobuf_name = command_selection_->itemText(selection_index).narrow();
    commands_div_->setCurrentIndex(commands_[protobuf_name]);
    //    master_field_info_stack_->setCurrentIndex(commands_[protobuf_name]);
}

void goby::apps::zeromq::LiaisonCommander::ControlsContainer::clear_message()
{
    WDialog dialog("Confirm clearing of message: " + command_selection_->currentText());
    WPushButton ok("Clear", dialog.contents());
    WPushButton cancel("Cancel", dialog.contents());

    dialog.rejectWhenEscapePressed();
    ok.clicked().connect(&dialog, &WDialog::accept);
    cancel.clicked().connect(&dialog, &WDialog::reject);

    if (dialog.exec() == WDialog::Accepted)
    {
        CommandContainer* current_command =
            dynamic_cast<CommandContainer*>(commands_div_->currentWidget());
        current_command->message_->Clear();
        current_command->generate_root();
    }
}

void goby::apps::zeromq::LiaisonCommander::ControlsContainer::send_message()
{
    glog.is(VERBOSE) && glog << "Message to be sent!" << std::endl;

    CommandContainer* current_command =
        dynamic_cast<CommandContainer*>(commands_div_->currentWidget());

    auto grouplayer =
        current_command->publish_to_.at(current_command->group_selection_->currentIndex());
    std::uint8_t group_numeric = grouplayer.group_numeric();

    // read the numeric group value out of the message if requested
    if (grouplayer.has_group_numeric_field_name())
    {
        auto desc = current_command->message_->GetDescriptor();
        auto group_numeric_field_desc =
            desc->FindFieldByName(grouplayer.group_numeric_field_name());

        auto refl = current_command->message_->GetReflection();
        switch (group_numeric_field_desc->cpp_type())
        {
            default: break;
            case google::protobuf::FieldDescriptor::CPPTYPE_INT32:
            {
                auto val = refl->GetInt32(*current_command->message_, group_numeric_field_desc);
                if (val >= std::numeric_limits<std::uint8_t>::min() &&
                    val <= std::numeric_limits<std::uint8_t>::max())
                    group_numeric = val;
                break;
            }

            case google::protobuf::FieldDescriptor::CPPTYPE_UINT32:
            {
                auto val = refl->GetUInt32(*current_command->message_, group_numeric_field_desc);
                if (val >= std::numeric_limits<std::uint8_t>::min() &&
                    val <= std::numeric_limits<std::uint8_t>::max())
                    group_numeric = val;

                break;
            }

            case google::protobuf::FieldDescriptor::CPPTYPE_INT64:
            {
                auto val = refl->GetInt64(*current_command->message_, group_numeric_field_desc);
                if (val >= std::numeric_limits<std::uint8_t>::min() &&
                    val <= std::numeric_limits<std::uint8_t>::max())
                    group_numeric = val;

                break;
            }

            case google::protobuf::FieldDescriptor::CPPTYPE_UINT64:
            {
                auto val = refl->GetUInt64(*current_command->message_, group_numeric_field_desc);
                if (val >= std::numeric_limits<std::uint8_t>::min() &&
                    val <= std::numeric_limits<std::uint8_t>::max())
                    group_numeric = val;

                break;
            }
            case google::protobuf::FieldDescriptor::CPPTYPE_ENUM:
            {
                auto val =
                    refl->GetEnum(*current_command->message_, group_numeric_field_desc)->number();
                if (val >= std::numeric_limits<std::uint8_t>::min() &&
                    val <= std::numeric_limits<std::uint8_t>::max())
                    group_numeric = val;

                break;
            }
        }
    }

    WDialog dialog("Confirm sending of message: " + command_selection_->currentText());

    WGroupBox* comment_box = new WGroupBox("Log comment", dialog.contents());
    WLineEdit* comment_line = new WLineEdit(comment_box);
    comment_line->setText(comment_line_->text());

    WGroupBox* group_box = new WGroupBox("Group", dialog.contents());
    WContainerWidget* group_div = new WContainerWidget(group_box);
    new WText("Group: " + to_string(grouplayer, group_numeric), group_div);

    WGroupBox* message_box = new WGroupBox("Message to send", dialog.contents());
    WContainerWidget* message_div = new WContainerWidget(message_box);
    new WText("<pre>" + current_command->message_->DebugString() + "</pre>", message_div);

    message_div->setMaximumSize(pb_commander_config_.modal_dimensions().width(),
                                pb_commander_config_.modal_dimensions().height());
    message_div->setOverflow(WContainerWidget::OverflowAuto);

    //    dialog.setResizable(true);

    WPushButton ok("Send", dialog.contents());
    WPushButton cancel("Cancel", dialog.contents());

    dialog.rejectWhenEscapePressed();

    ok.clicked().connect(&dialog, &WDialog::accept);
    cancel.clicked().connect(&dialog, &WDialog::reject);

    if (dialog.exec() == WDialog::Accepted)
    {
        switch (grouplayer.layer())
        {
            case goby::middleware::protobuf::LAYER_INTERTHREAD:
                commander_->post_to_comms([=]() {
                    commander_->goby_thread()
                        ->interthread()
                        .publish_dynamic<google::protobuf::Message>(
                            current_command->message_,
                            goby::middleware::DynamicGroup(grouplayer.group()));
                });
                break;
            case goby::middleware::protobuf::LAYER_INTERPROCESS:
                commander_->post_to_comms([=]() {
                    commander_->goby_thread()
                        ->interprocess()
                        .publish_dynamic<google::protobuf::Message>(
                            current_command->message_,
                            goby::middleware::DynamicGroup(grouplayer.group()));
                });
                break;
            case goby::middleware::protobuf::LAYER_INTERVEHICLE:
                commander_->post_to_comms([=]() {
                    commander_->goby_thread()
                        ->intervehicle()
                        .publish_dynamic<google::protobuf::Message,
                                         goby::middleware::MarshallingScheme::DCCL>(
                            current_command->message_,
                            goby::middleware::DynamicGroup(grouplayer.group(), group_numeric),
                            commander_->goby_thread()->command_publisher_);
                });
                break;
        }

        CommandEntry* command_entry = new CommandEntry;
        command_entry->protobuf_name = current_command->message_->GetDescriptor()->full_name();
        command_entry->bytes.resize(current_command->message_->ByteSize());
        current_command->message_->SerializeToArray(&command_entry->bytes[0],
                                                    command_entry->bytes.size());
        command_entry->address = wApp->environment().clientAddress();
        command_entry->group = grouplayer.group();
        command_entry->layer = goby::middleware::to_string(grouplayer.layer());

        boost::posix_time::ptime now = goby::time::SystemClock::now<boost::posix_time::ptime>();
        command_entry->time.setPosixTime(now);
        command_entry->utime = current_command->latest_time_;

        command_entry->comment = comment_line->text().narrow();
        command_entry->last_ack = 0;
        session_.add(command_entry);

        {
            std::lock_guard<std::mutex> slock(dbo_mutex_);
            Dbo::Transaction transaction(*current_command->session_);
            transaction.commit();
            last_db_update_time_ = now;
        }

        comment_line_->setText("");
        current_command->query_model_->reload();
    }
}

goby::apps::zeromq::LiaisonCommander::ControlsContainer::CommandContainer::CommandContainer(
    const protobuf::ProtobufCommanderConfig& pb_commander_config, const std::string& protobuf_name,
    Dbo::Session* session)
    //    WStackedWidget* master_field_info_stack)
    : WGroupBox(protobuf_name),
      message_(dccl::DynamicProtobufManager::new_protobuf_message<
               std::shared_ptr<google::protobuf::Message>>(protobuf_name)),
      latest_time_(0),
      group_div_(new WContainerWidget(this)),
      group_label_(new WLabel("Group: ", group_div_)),
      group_selection_(new WComboBox(group_div_)),
      tree_box_(new WGroupBox("Contents", this)),
      tree_table_(new WTreeTable(tree_box_)),
      //      field_info_stack_(new WStackedWidget(master_field_info_stack)),
      session_(session),
      query_model_(new Dbo::QueryModel<Dbo::ptr<CommandEntry>>(this)),
      query_box_(new WGroupBox("Sent message log (click for details)", this)),
      query_table_(new WTreeView(query_box_)),
      last_reload_time_(boost::posix_time::neg_infin),
      pb_commander_config_(pb_commander_config)
{
    //    new WText("", field_info_stack_);
    //field_info_map_[0] = 0;

    tree_table_->addColumn("Value", pb_commander_config.value_width_pixels());
    tree_table_->addColumn("Modify", pb_commander_config.modify_width_pixels());

    {
        std::lock_guard<std::mutex> slock(dbo_mutex_);
        Dbo::Transaction transaction(*session_);
        query_model_->setQuery(
            session_->find<CommandEntry>("where protobuf_name='" + protobuf_name + "'"));
    }

    query_model_->addColumn("comment", "Comment");
    query_model_->addColumn("protobuf_name", "Name");
    query_model_->addColumn("group", "Group");
    query_model_->addColumn("layer", "Layer");
    query_model_->addColumn("address", "Network Address");
    query_model_->addColumn("time", "Time");
    //query_model_->addColumn("last_ack", "Latest Ack");

    query_table_->setModel(query_model_);
    query_table_->resize(WLength::Auto, pb_commander_config.database_view_height());
    query_table_->sortByColumn(protobuf::ProtobufCommanderConfig::COLUMN_TIME, DescendingOrder);
    query_table_->setMinimumSize(pb_commander_config.database_width().comment_width() +
                                     pb_commander_config.database_width().name_width() +
                                     pb_commander_config.database_width().group_width() +
                                     pb_commander_config.database_width().layer_width() +
                                     pb_commander_config.database_width().ip_width() +
                                     pb_commander_config.database_width().time_width() +
                                     pb_commander_config.database_width().last_ack_width() +
                                     7 * (protobuf::ProtobufCommanderConfig::COLUMN_MAX + 1),
                                 WLength::Auto);

    query_table_->setColumnWidth(protobuf::ProtobufCommanderConfig::COLUMN_COMMENT,
                                 pb_commander_config.database_width().comment_width());
    query_table_->setColumnWidth(protobuf::ProtobufCommanderConfig::COLUMN_NAME,
                                 pb_commander_config.database_width().name_width());
    query_table_->setColumnWidth(protobuf::ProtobufCommanderConfig::COLUMN_GROUP,
                                 pb_commander_config.database_width().group_width());
    query_table_->setColumnWidth(protobuf::ProtobufCommanderConfig::COLUMN_LAYER,
                                 pb_commander_config.database_width().layer_width());
    query_table_->setColumnWidth(protobuf::ProtobufCommanderConfig::COLUMN_IP,
                                 pb_commander_config.database_width().ip_width());

    query_table_->setColumnWidth(protobuf::ProtobufCommanderConfig::COLUMN_TIME,
                                 pb_commander_config.database_width().time_width());

    query_table_->setColumnWidth(protobuf::ProtobufCommanderConfig::COLUMN_LAST_ACK,
                                 pb_commander_config.database_width().last_ack_width());

    query_table_->clicked().connect(this, &CommandContainer::handle_database_double_click);

    if (query_model_->rowCount() > 0)
    {
        const Dbo::ptr<CommandEntry>& entry = query_model_->resultRow(0);
        message_->ParseFromArray(&entry->bytes[0], entry->bytes.size());

        int group_index =
            group_selection_->findText(to_string(to_group_layer(entry->group, entry->layer)));
        if (group_index >= 0)
            group_selection_->setCurrentIndex(group_index);
    }

    glog.is(DEBUG1) && glog << "Model has " << query_model_->rowCount() << " rows" << std::endl;

    generate_root();
}

void goby::apps::zeromq::LiaisonCommander::ControlsContainer::CommandContainer::
    handle_database_double_click(const WModelIndex& index, const WMouseEvent& event)
{
    glog.is(DEBUG1) && glog << "clicked: " << index.row() << "," << index.column()
                            << ", is_valid: " << index.isValid() << std::endl;

    if (!index.isValid())
        return;

    const Dbo::ptr<CommandEntry>& entry = query_model_->resultRow(index.row());

    std::shared_ptr<google::protobuf::Message> message(message_->New());
    message->ParseFromArray(&entry->bytes[0], entry->bytes.size());
    std::string group = entry->group;
    std::string layer = entry->layer;

    if (!message)
    {
        glog.is(WARN) && glog << "Invalid message!" << std::endl;
        return;
    }

    database_dialog_.reset(new WDialog("Viewing log entry: " + entry->protobuf_name +
                                       " posted at " + entry->time.toString()));

    WGroupBox* comment_box = new WGroupBox("Log comment", database_dialog_->contents());
    new WText(entry->comment, comment_box);

    WContainerWidget* contents_div = new WContainerWidget(database_dialog_->contents());
    WGroupBox* message_box = new WGroupBox("Message posted to " + group, contents_div);

    WContainerWidget* message_div = new WContainerWidget(message_box);

    new WText("<pre>" + message->DebugString() + "</pre>", message_div);

    protobuf::NetworkAckSet acks;
    acks.ParseFromArray(&entry->acks[0], entry->acks.size());

    WGroupBox* acks_box = new WGroupBox("Acks posted", contents_div);
    WContainerWidget* acks_div = new WContainerWidget(acks_box);
    new WText("<pre>" + acks.DebugString() + "</pre>", acks_div);

    contents_div->setMaximumSize(pb_commander_config_.modal_dimensions().width(),
                                 pb_commander_config_.modal_dimensions().height());
    contents_div->setOverflow(WContainerWidget::OverflowAuto);

    WPushButton* edit = new WPushButton("Edit (replace)", database_dialog_->contents());
    WPushButton* merge = new WPushButton("Edit (merge)", database_dialog_->contents());
    WPushButton* cancel = new WPushButton("Cancel", database_dialog_->contents());

    database_dialog_->rejectWhenEscapePressed();

    edit->clicked().connect(boost::bind(&CommandContainer::handle_database_dialog, this,
                                        RESPONSE_EDIT, message, group, layer));
    merge->clicked().connect(boost::bind(&CommandContainer::handle_database_dialog, this,
                                         RESPONSE_MERGE, message, group, layer));
    cancel->clicked().connect(boost::bind(&CommandContainer::handle_database_dialog, this,
                                          RESPONSE_CANCEL, message, group, layer));

    database_dialog_->show();
    //     merge.clicked().connect(&dialog, &WDialog::accept);
    //     cancel.clicked().connect(&dialog, &WDialog::reject);
}

void goby::apps::zeromq::LiaisonCommander::ControlsContainer::CommandContainer::
    handle_database_dialog(DatabaseDialogResponse response,
                           std::shared_ptr<google::protobuf::Message> message, std::string group,
                           std::string layer)
{
    switch (response)
    {
        case RESPONSE_EDIT:
        {
            message_->CopyFrom(*message);
            int group_index = group_selection_->findText(to_string(to_group_layer(group, layer)));

            glog.is_debug1() && glog << "Group: " << group << ", index: " << group_index
                                     << std::endl;
            if (group_index >= 0)
                group_selection_->setCurrentIndex(group_index);

            generate_root();
            database_dialog_->accept();
            break;
        }

        case RESPONSE_MERGE:
        {
            message->MergeFrom(*message_);
            message_->CopyFrom(*message);

            int group_index = group_selection_->findText(to_string(to_group_layer(group, layer)));
            if (group_index >= 0)
                group_selection_->setCurrentIndex(group_index);

            generate_root();
            database_dialog_->accept();
            break;
        }

        case RESPONSE_CANCEL: database_dialog_->reject(); break;
    }
}

void goby::apps::zeromq::LiaisonCommander::ControlsContainer::CommandContainer::generate_root()
{
    const google::protobuf::Descriptor* desc = message_->GetDescriptor();

    // Create and set the root node
    WTreeTableNode* root = new WTreeTableNode(desc->name());
    root->setImagePack("resources/");
    root->setStyleClass(STRIPE_EVEN_CLASS);

    // deletes an existing root
    tree_table_->setTreeRoot(root, "Field");

    time_fields_.clear();

    generate_tree(root, message_.get());

    root->expand();
}

void goby::apps::zeromq::LiaisonCommander::ControlsContainer::CommandContainer::generate_tree(
    WTreeTableNode* parent, google::protobuf::Message* message)
{
    const google::protobuf::Descriptor* desc = message->GetDescriptor();

    for (int i = 0, n = desc->field_count(); i < n; ++i)
        generate_tree_row(parent, message, desc->field(i));

    std::vector<const google::protobuf::FieldDescriptor*> extensions;
    dccl::DynamicProtobufManager::user_descriptor_pool().FindAllExtensions(desc, &extensions);
    google::protobuf::DescriptorPool::generated_pool()->FindAllExtensions(desc, &extensions);
    for (int i = 0, n = extensions.size(); i < n; ++i)
        generate_tree_row(parent, message, extensions[i]);
}

void goby::apps::zeromq::LiaisonCommander::ControlsContainer::CommandContainer::generate_tree_row(
    WTreeTableNode* parent, google::protobuf::Message* message,
    const google::protobuf::FieldDescriptor* field_desc)
{
    const google::protobuf::Reflection* refl = message->GetReflection();

    if (field_desc->options().GetExtension(dccl::field).omit())
        return;

    int index = parent->childNodes().size();

    LiaisonTreeTableNode* node =
        new LiaisonTreeTableNode(field_desc->is_extension() ? "[" + field_desc->full_name() + "]: "
                                                            : field_desc->name() + ": ",
                                 0, parent);

    if ((parent->styleClass() == STRIPE_ODD_CLASS && index % 2) ||
        (parent->styleClass() == STRIPE_EVEN_CLASS && !(index % 2)))
        node->setStyleClass(STRIPE_ODD_CLASS);
    else
        node->setStyleClass(STRIPE_EVEN_CLASS);

    WFormWidget* value_field = 0;
    WFormWidget* modify_field = 0;
    if (field_desc->is_repeated())
    {
        //        WContainerWidget* div = new WContainerWidget;
        //        WLabel* label = new WLabel(": ", div);
        WSpinBox* spin_box = new WSpinBox;
        spin_box->setTextSize(3);
        //       label->setBuddy(spin_box);
        spin_box->setRange(0, std::numeric_limits<int>::max());
        spin_box->setSingleStep(1);

        spin_box->valueChanged().connect(boost::bind(&CommandContainer::handle_repeated_size_change,
                                                     this, _1, message, field_desc, node));

        spin_box->setValue(refl->FieldSize(*message, field_desc));
        spin_box->valueChanged().emit(refl->FieldSize(*message, field_desc));

        modify_field = spin_box;
    }
    else
    {
        if (field_desc->cpp_type() == google::protobuf::FieldDescriptor::CPPTYPE_MESSAGE)
        {
            if (field_desc->is_required())
            {
                generate_tree(node, message->GetReflection()->MutableMessage(message, field_desc));
                node->expand();
            }
            else
            {
                WPushButton* button = new WPushButton(MESSAGE_INCLUDE_TEXT);

                button->clicked().connect(
                    boost::bind(&CommandContainer::handle_toggle_single_message, this, _1, message,
                                field_desc, button, node));

                if (refl->HasField(*message, field_desc))
                {
                    parent->expand();
                    handle_toggle_single_message(WMouseEvent(), message, field_desc, button, node);
                }

                modify_field = button;
            }
        }
        else
        {
            generate_tree_field(value_field, message, field_desc);
        }
    }
    if (value_field)
        node->setColumnWidget(1, value_field);

    if (modify_field)
    {
        dccl_default_modify_field(modify_field, field_desc);

        generate_field_info_box(modify_field, field_desc);

        node->setColumnWidget(2, modify_field);
    }
}

void goby::apps::zeromq::LiaisonCommander::ControlsContainer::CommandContainer::generate_tree_field(
    WFormWidget*& value_field, google::protobuf::Message* message,
    const google::protobuf::FieldDescriptor* field_desc, int index /*= -1*/)
{
    const google::protobuf::Reflection* refl = message->GetReflection();

    switch (field_desc->cpp_type())
    {
        case google::protobuf::FieldDescriptor::CPPTYPE_MESSAGE: break;

        case google::protobuf::FieldDescriptor::CPPTYPE_INT32:
        {
            WIntValidator* validator = new WIntValidator;

            if (field_desc->is_repeated() && refl->FieldSize(*message, field_desc) <= index)
                refl->AddInt32(message, field_desc, field_desc->default_value_int32());

            std::int32_t value = field_desc->is_repeated()
                                     ? refl->GetRepeatedInt32(*message, field_desc, index)
                                     : refl->GetInt32(*message, field_desc);

            value_field = generate_single_line_edit_field(
                message, field_desc, goby::util::as<std::string>(value),
                goby::util::as<std::string>(field_desc->default_value_int32()), validator, index);
        }
        break;

        case google::protobuf::FieldDescriptor::CPPTYPE_INT64:
        {
            WIntValidator* validator = 0;

            if (field_desc->is_repeated() && refl->FieldSize(*message, field_desc) <= index)
                refl->AddInt64(message, field_desc, field_desc->default_value_int64());

            std::int64_t value = field_desc->is_repeated()
                                     ? refl->GetRepeatedInt64(*message, field_desc, index)
                                     : refl->GetInt64(*message, field_desc);

            value_field = generate_single_line_edit_field(
                message, field_desc, goby::util::as<std::string>(value),
                goby::util::as<std::string>(field_desc->default_value_int64()), validator, index);
        }
        break;

        case google::protobuf::FieldDescriptor::CPPTYPE_UINT32:
        {
            WIntValidator* validator = new WIntValidator;
            validator->setBottom(0);

            if (field_desc->is_repeated() && refl->FieldSize(*message, field_desc) <= index)
                refl->AddUInt32(message, field_desc, field_desc->default_value_uint32());

            std::uint32_t value = field_desc->is_repeated()
                                      ? refl->GetRepeatedUInt32(*message, field_desc, index)
                                      : refl->GetUInt32(*message, field_desc);

            value_field = generate_single_line_edit_field(
                message, field_desc, goby::util::as<std::string>(value),
                goby::util::as<std::string>(field_desc->default_value_uint32()), validator, index);
        }
        break;

        case google::protobuf::FieldDescriptor::CPPTYPE_UINT64:
        {
            WIntValidator* validator = 0;

            if (field_desc->is_repeated() && refl->FieldSize(*message, field_desc) <= index)
                refl->AddUInt64(message, field_desc, field_desc->default_value_uint64());

            std::uint64_t value = field_desc->is_repeated()
                                      ? refl->GetRepeatedUInt64(*message, field_desc, index)
                                      : refl->GetUInt64(*message, field_desc);

            value_field = generate_single_line_edit_field(
                message, field_desc, goby::util::as<std::string>(value),
                goby::util::as<std::string>(field_desc->default_value_uint64()), validator, index);
        }
        break;

        case google::protobuf::FieldDescriptor::CPPTYPE_STRING:
        {
            if (field_desc->is_repeated() && refl->FieldSize(*message, field_desc) <= index)
                refl->AddString(message, field_desc, field_desc->default_value_string());

            WValidator* validator = 0;

            std::string current_str = field_desc->is_repeated()
                                          ? refl->GetRepeatedString(*message, field_desc, index)
                                          : refl->GetString(*message, field_desc);
            std::string default_str = field_desc->default_value_string();

            if (field_desc->type() == google::protobuf::FieldDescriptor::TYPE_BYTES)
            {
                validator = new WRegExpValidator("([0-9,a-f,A-F][0-9,a-f,A-F])*");

                current_str = goby::util::hex_encode(current_str);
                default_str = goby::util::hex_encode(default_str);
            }
            else
            {
                validator = new WValidator;
            }

            value_field = generate_single_line_edit_field(message, field_desc, current_str,
                                                          default_str, validator, index);
        }
        break;

        case google::protobuf::FieldDescriptor::CPPTYPE_FLOAT:
        {
            if (field_desc->is_repeated() && refl->FieldSize(*message, field_desc) <= index)
                refl->AddFloat(message, field_desc, field_desc->default_value_float());

            float value = field_desc->is_repeated()
                              ? refl->GetRepeatedFloat(*message, field_desc, index)
                              : refl->GetFloat(*message, field_desc);

            WDoubleValidator* validator = new WDoubleValidator;
            validator->setRange(std::numeric_limits<float>::min(),
                                std::numeric_limits<float>::max());

            value_field = generate_single_line_edit_field(
                message, field_desc,
                goby::util::as<std::string>(value, std::numeric_limits<float>::digits10),
                goby::util::as<std::string>(field_desc->default_value_float(),
                                            std::numeric_limits<float>::digits10),
                validator, index);
        }
        break;

        case google::protobuf::FieldDescriptor::CPPTYPE_DOUBLE:
        {
            if (field_desc->is_repeated() && refl->FieldSize(*message, field_desc) <= index)
                refl->AddDouble(message, field_desc, field_desc->default_value_double());

            double value = field_desc->is_repeated()
                               ? refl->GetRepeatedDouble(*message, field_desc, index)
                               : refl->GetDouble(*message, field_desc);

            WDoubleValidator* validator = new WDoubleValidator;

            value_field = generate_single_line_edit_field(
                message, field_desc,
                goby::util::as<std::string>(value, std::numeric_limits<double>::digits10),
                goby::util::as<std::string>(field_desc->default_value_double(),
                                            std::numeric_limits<double>::digits10),
                validator, index);
        }
        break;

        case google::protobuf::FieldDescriptor::CPPTYPE_BOOL:
        {
            if (field_desc->is_repeated() && refl->FieldSize(*message, field_desc) <= index)
                refl->AddBool(message, field_desc, field_desc->default_value_bool());

            bool value = field_desc->is_repeated()
                             ? refl->GetRepeatedBool(*message, field_desc, index)
                             : refl->GetBool(*message, field_desc);

            std::vector<WString> strings;
            strings.push_back("true");
            strings.push_back("false");

            value_field = generate_combo_box_field(
                message, field_desc, strings, value ? 0 : 1,
                goby::util::as<std::string>(field_desc->default_value_bool()), index);
        }
        break;

        case google::protobuf::FieldDescriptor::CPPTYPE_ENUM:
        {
            if (field_desc->is_repeated() && refl->FieldSize(*message, field_desc) <= index)
                refl->AddEnum(message, field_desc, field_desc->default_value_enum());

            const google::protobuf::EnumValueDescriptor* value =
                field_desc->is_repeated() ? refl->GetRepeatedEnum(*message, field_desc, index)
                                          : refl->GetEnum(*message, field_desc);

            std::vector<WString> strings;

            const google::protobuf::EnumDescriptor* enum_desc = field_desc->enum_type();

            for (int i = 0, n = enum_desc->value_count(); i < n; ++i)
                strings.push_back(enum_desc->value(i)->name());

            value_field = generate_combo_box_field(
                message, field_desc, strings, value->index(),
                goby::util::as<std::string>(field_desc->default_value_enum()->name()), index);
        }
        break;
    }

    dccl_default_value_field(value_field, field_desc);
    //    queue_default_value_field(value_field, field_desc);

    generate_field_info_box(value_field, field_desc);
}
void goby::apps::zeromq::LiaisonCommander::ControlsContainer::CommandContainer::
    generate_field_info_box(Wt::WFormWidget*& value_field,
                            const google::protobuf::FieldDescriptor* field_desc)
{
    //    if(!field_info_map_.count(field_desc))
    //    {
    //        WGroupBox* box = new WGroupBox(field_desc->full_name(), field_info_stack_);

    std::string info;

    //    new WText("[Field] " + field_desc->DebugString(), box);

    std::vector<const google::protobuf::FieldDescriptor*> extensions;
    google::protobuf::DescriptorPool::generated_pool()->FindAllExtensions(
        field_desc->options().GetDescriptor(), &extensions);
    for (int i = 0, n = extensions.size(); i < n; ++i)
    {
        const google::protobuf::FieldDescriptor* ext_field_desc = extensions[i];
        if (!ext_field_desc->is_repeated() &&
            field_desc->options().GetReflection()->HasField(field_desc->options(), ext_field_desc))
        {
            std::string ext_str;
            google::protobuf::TextFormat::PrintFieldValueToString(field_desc->options(),
                                                                  ext_field_desc, -1, &ext_str);

            if (!info.empty())
                info += "<br/>";

            info += "[Options] " + ext_field_desc->full_name() + ": " + ext_str;

            //new WText("<br/> [Options] " + ext_field_desc->full_name() + ": " + ext_str, box);
        }
    }

    //     field_info_map_.insert(std::make_pair(field_desc, field_info_map_.size()));
    // }

    //#if WT_VERSION >= 0x03011000
    //    if(!info.empty())
    //        value_field->setToolTip(info, Wt::XHTMLText);
    //#else
    //    if(!info.empty())
    //        value_field->setToolTip(info);
    //#endif

    //    value_field->focussed().connect(boost::bind(&CommandContainer::handle_field_focus, this,
    //                                                field_info_map_[field_desc]));
    //  value_field->blurred().connect(boost::bind(&CommandContainer::handle_field_focus, this, 0));
}

void goby::apps::zeromq::LiaisonCommander::ControlsContainer::CommandContainer::
    handle_line_field_changed(google::protobuf::Message* message,
                              const google::protobuf::FieldDescriptor* field_desc, WLineEdit* field,
                              int index)
{
    std::string value = field->text().narrow();

    const google::protobuf::Reflection* refl = message->GetReflection();

    if (value.empty() && field_desc->is_repeated())
        value = field->emptyText().narrow();

    if (value.empty() && !field_desc->is_repeated())
    {
        refl->ClearField(message, field_desc);
    }
    else
    {
        switch (field_desc->cpp_type())
        {
            case google::protobuf::FieldDescriptor::CPPTYPE_INT32:
                field_desc->is_repeated()
                    ? refl->SetRepeatedInt32(message, field_desc, index,
                                             goby::util::as<std::int32_t>(value))
                    : refl->SetInt32(message, field_desc, goby::util::as<std::int32_t>(value));
                break;

            case google::protobuf::FieldDescriptor::CPPTYPE_INT64:
                field_desc->is_repeated()
                    ? refl->SetRepeatedInt64(message, field_desc, index,
                                             goby::util::as<std::int64_t>(value))
                    : refl->SetInt64(message, field_desc, goby::util::as<std::int64_t>(value));
                break;

            case google::protobuf::FieldDescriptor::CPPTYPE_UINT32:
                field_desc->is_repeated()
                    ? refl->SetRepeatedUInt32(message, field_desc, index,
                                              goby::util::as<std::uint32_t>(value))
                    : refl->SetUInt32(message, field_desc, goby::util::as<std::uint32_t>(value));
                break;

            case google::protobuf::FieldDescriptor::CPPTYPE_UINT64:
                field_desc->is_repeated()
                    ? refl->SetRepeatedUInt64(message, field_desc, index,
                                              goby::util::as<std::uint64_t>(value))
                    : refl->SetUInt64(message, field_desc, goby::util::as<std::uint64_t>(value));
                break;

            case google::protobuf::FieldDescriptor::CPPTYPE_STRING:

                if (field_desc->type() == google::protobuf::FieldDescriptor::TYPE_BYTES)
                {
                    value = goby::util::hex_decode(value);
                }

                field_desc->is_repeated()
                    ? refl->SetRepeatedString(message, field_desc, index, value)
                    : refl->SetString(message, field_desc, value);
                break;

            case google::protobuf::FieldDescriptor::CPPTYPE_FLOAT:
            {
                double fvalue = goby::util::as<float>(value);

                if (field_desc->options().GetExtension(dccl::field).has_precision())
                    field->setText(string_from_dccl_double(&fvalue, field_desc));

                field_desc->is_repeated()
                    ? refl->SetRepeatedFloat(message, field_desc, index, fvalue)
                    : refl->SetFloat(message, field_desc, fvalue);
            }
            break;

            case google::protobuf::FieldDescriptor::CPPTYPE_DOUBLE:
            {
                double dvalue = goby::util::as<double>(value);

                if (field_desc->options().GetExtension(dccl::field).has_precision())
                    field->setText(string_from_dccl_double(&dvalue, field_desc));

                field_desc->is_repeated()
                    ? refl->SetRepeatedDouble(message, field_desc, index, dvalue)
                    : refl->SetDouble(message, field_desc, dvalue);
            }
            break;

            default: break;
        }
    }
}

void goby::apps::zeromq::LiaisonCommander::ControlsContainer::CommandContainer::
    handle_combo_field_changed(google::protobuf::Message* message,
                               const google::protobuf::FieldDescriptor* field_desc,
                               WComboBox* field, int index)
{
    const google::protobuf::Reflection* refl = message->GetReflection();

    if (field->currentIndex() == 0)
        refl->ClearField(message, field_desc);
    else
    {
        std::string value = field->currentText().narrow();

        switch (field_desc->cpp_type())
        {
            case google::protobuf::FieldDescriptor::CPPTYPE_BOOL:
                field_desc->is_repeated()
                    ? refl->SetRepeatedBool(message, field_desc, index, goby::util::as<bool>(value))
                    : refl->SetBool(message, field_desc, goby::util::as<bool>(value));
                break;

            case google::protobuf::FieldDescriptor::CPPTYPE_ENUM:
                field_desc->is_repeated()
                    ? refl->SetRepeatedEnum(message, field_desc, index,
                                            field_desc->enum_type()->FindValueByName(value))
                    : refl->SetEnum(message, field_desc,
                                    field_desc->enum_type()->FindValueByName(value));
                break;

            default: break;
        }
    }
    glog.is(DEBUG1) && glog << "The message is: " << message_->DebugString() << std::endl;
}

WLineEdit* goby::apps::zeromq::LiaisonCommander::ControlsContainer::CommandContainer::
    generate_single_line_edit_field(google::protobuf::Message* message,
                                    const google::protobuf::FieldDescriptor* field_desc,
                                    const std::string& current_value,
                                    const std::string& default_value,
                                    WValidator* validator /* = 0*/, int index /*= -1*/)
{
    const google::protobuf::Reflection* refl = message->GetReflection();

    WLineEdit* line_edit = new WLineEdit();

    if (field_desc->has_default_value() || field_desc->is_repeated())
        line_edit->setEmptyText(default_value);

    if ((!field_desc->is_repeated() && refl->HasField(*message, field_desc)) ||
        (field_desc->is_repeated() && index < refl->FieldSize(*message, field_desc)))
        line_edit->setText(current_value);

    if (validator)
    {
        validator->setMandatory(field_desc->is_required());
        line_edit->setValidator(validator);
    }

    line_edit->changed().connect(boost::bind(&CommandContainer::handle_line_field_changed, this,
                                             message, field_desc, line_edit, index));

    return line_edit;
}

WComboBox*
goby::apps::zeromq::LiaisonCommander::ControlsContainer::CommandContainer::generate_combo_box_field(
    google::protobuf::Message* message, const google::protobuf::FieldDescriptor* field_desc,
    const std::vector<WString>& strings, int current_value, const std::string& default_value,
    int index /*= -1*/)
{
    const google::protobuf::Reflection* refl = message->GetReflection();

    WComboBox* combo_box = new WComboBox;
    WStringListModel* model = new WStringListModel(strings, this);

    if (field_desc->has_default_value())
        model->insertString(0, "(default: " + default_value + ")");
    else
        model->insertString(0, "");

    combo_box->setModel(model);

    if ((!field_desc->is_repeated() && refl->HasField(*message, field_desc)) ||
        (field_desc->is_repeated() && index < refl->FieldSize(*message, field_desc)))
        combo_box->setCurrentIndex(current_value + 1);

    combo_box->changed().connect(boost::bind(&CommandContainer::handle_combo_field_changed, this,
                                             message, field_desc, combo_box, index));

    return combo_box;
}

// void goby::apps::zeromq::LiaisonCommander::ControlsContainer::CommandContainer::queue_default_value_field(WFormWidget*& value_field,
//     const google::protobuf::FieldDescriptor* field_desc)
// {
// const QueueFieldOptions& queue_options = field_desc->options().GetExtension(goby::field).queue();
// if(queue_options.is_time())
// {
//     value_field->setDisabled(true);
//     set_time_field(value_field, field_desc);
//     time_field_ = std::make_pair(value_field, field_desc);
// }
// else if(queue_options.is_src())
// {
//     if(WLineEdit* line_edit = dynamic_cast<WLineEdit*>(value_field))
//     {
//         if(pb_commander_config_.has_modem_id())
//         {
//             line_edit->setDisabled(true);
//             line_edit->setText(goby::util::as<std::string>(pb_commander_config_.modem_id()));
//             line_edit->changed().emit();
//         }
//     }
// }
//}

void goby::apps::zeromq::LiaisonCommander::ControlsContainer::CommandContainer::set_time_field(
    WFormWidget* value_field, const google::protobuf::FieldDescriptor* field_desc)
{
    if (WLineEdit* line_edit = dynamic_cast<WLineEdit*>(value_field))
    {
        boost::posix_time::ptime now = goby::time::SystemClock::now<boost::posix_time::ptime>();

        const dccl::DCCLFieldOptions& options = field_desc->options().GetExtension(dccl::field);
        latest_time_ = time::convert<time::MicroTime>(now).value();
        enum
        {
            MICROSEC_ORDER_MAG = 6
        };

        switch (field_desc->cpp_type())
        {
            default: line_edit->setText("Error: invalid goby-acomms time type"); break;

            case google::protobuf::FieldDescriptor::CPPTYPE_UINT64:
            case google::protobuf::FieldDescriptor::CPPTYPE_INT64:
                line_edit->setText(
                    goby::util::as<std::string>(time::convert<time::MicroTime>(now).value()));
                if (!options.has_precision())
                    latest_time_ = dccl::round(latest_time_, -MICROSEC_ORDER_MAG);
                else
                    latest_time_ = dccl::round(latest_time_, options.precision());
                break;

            case google::protobuf::FieldDescriptor::CPPTYPE_STRING:
                line_edit->setText(goby::util::as<std::string>(now));
                break;

            case google::protobuf::FieldDescriptor::CPPTYPE_DOUBLE:
                line_edit->setText(goby::util::as<std::string>(
                    dccl::round(time::convert<time::SITime>(now).value(), 0)));

                latest_time_ = dccl::round(latest_time_, options.precision() - MICROSEC_ORDER_MAG);

                break;
        }
        line_edit->changed().emit();
    }
}

void goby::apps::zeromq::LiaisonCommander::ControlsContainer::CommandContainer::
    dccl_default_value_field(WFormWidget*& value_field,
                             const google::protobuf::FieldDescriptor* field_desc)
{
    const dccl::DCCLFieldOptions& options = field_desc->options().GetExtension(dccl::field);

    if (options.has_min() && options.has_max())
    {
        WValidator* validator = value_field->validator();
        if (WIntValidator* int_validator = dynamic_cast<WIntValidator*>(validator))
            int_validator->setRange(options.min(), options.max());
        if (WDoubleValidator* double_validator = dynamic_cast<WDoubleValidator*>(validator))
            double_validator->setRange(options.min(), options.max());
    }

    if (options.has_static_value())
    {
        if (WLineEdit* line_edit = dynamic_cast<WLineEdit*>(value_field))
        {
            line_edit->setText(options.static_value());
            line_edit->changed().emit();
        }

        else if (WComboBox* combo_box = dynamic_cast<WComboBox*>(value_field))
        {
            combo_box->setCurrentIndex(combo_box->findText(options.static_value()));
            combo_box->changed().emit();
        }

        value_field->setDisabled(true);
    }

    if (options.has_max_length())
    {
        if (field_desc->type() == google::protobuf::FieldDescriptor::TYPE_STRING)
        {
            WLengthValidator* validator = new WLengthValidator(0, options.max_length());
            value_field->setValidator(validator);
        }
        else if (field_desc->type() == google::protobuf::FieldDescriptor::TYPE_BYTES)
        {
            WRegExpValidator* validator =
                new WRegExpValidator("([0-9,a-f,A-F][0-9,a-f,A-F]){0," +
                                     goby::util::as<std::string>(options.max_length()) + "}");

            value_field->setValidator(validator);
        }
    }

    if (options.codec() == "_time" || options.codec() == "dccl.time2")
    {
        value_field->setDisabled(true);
        set_time_field(value_field, field_desc);
        time_fields_.insert(std::make_pair(value_field, field_desc));
    }
}

void goby::apps::zeromq::LiaisonCommander::ControlsContainer::CommandContainer::
    dccl_default_modify_field(WFormWidget*& modify_field,
                              const google::protobuf::FieldDescriptor* field_desc)
{
    const dccl::DCCLFieldOptions& options = field_desc->options().GetExtension(dccl::field);

    if (options.has_max_repeat())
    {
        if (WSpinBox* spin_box = dynamic_cast<WSpinBox*>(modify_field))
            spin_box->setMaximum(options.max_repeat());
    }
}

std::string
goby::apps::zeromq::LiaisonCommander::ControlsContainer::CommandContainer::string_from_dccl_double(
    double* value, const google::protobuf::FieldDescriptor* field_desc)
{
    const dccl::DCCLFieldOptions& options = field_desc->options().GetExtension(dccl::field);
    *value = dccl::round(*value, options.precision());

    if (options.precision() < 0)
    {
        return goby::util::as<std::string>(
            *value, std::max(0.0, std::log10(std::abs(*value)) + options.precision()),
            goby::util::FLOAT_SCIENTIFIC);
    }
    else
    {
        return goby::util::as<std::string>(*value, options.precision(), goby::util::FLOAT_FIXED);
    }
}

// void goby::apps::zeromq::LiaisonCommander::ControlsContainer::CommandContainer::handle_field_focus(int field_info_index)
// {
//     field_info_stack_->setCurrentIndex(field_info_index);
// }

void goby::apps::zeromq::LiaisonCommander::ControlsContainer::CommandContainer::
    handle_repeated_size_change(int desired_size, google::protobuf::Message* message,
                                const google::protobuf::FieldDescriptor* field_desc,
                                WTreeTableNode* parent)
{
    const google::protobuf::Reflection* refl = message->GetReflection();

    // add nodes
    while (desired_size > static_cast<int>(parent->childNodes().size()))
    {
        int index = parent->childNodes().size();
        WTreeTableNode* node =
            new WTreeTableNode("index: " + goby::util::as<std::string>(index), 0, parent);

        if ((parent->styleClass() == STRIPE_ODD_CLASS && index % 2) ||
            (parent->styleClass() == STRIPE_EVEN_CLASS && !(index % 2)))
            node->setStyleClass(STRIPE_ODD_CLASS);
        else
            node->setStyleClass(STRIPE_EVEN_CLASS);

        WFormWidget* value_field = 0;

        if (field_desc->cpp_type() == google::protobuf::FieldDescriptor::CPPTYPE_MESSAGE)
        {
            if (refl->FieldSize(*message, field_desc) <= index)
            {
                generate_tree(node, refl->AddMessage(message, field_desc));
            }
            else
            {
                parent->expand();
                generate_tree(node, refl->MutableRepeatedMessage(message, field_desc, index));
            }
        }
        else
        {
            generate_tree_field(value_field, message, field_desc, index);
        }

        if (value_field)
            node->setColumnWidget(1, value_field);
        parent->expand();
        node->expand();
    }

    // remove nodes
    while (desired_size < static_cast<int>(parent->childNodes().size()))
    {
        parent->removeChildNode(parent->childNodes().back());

        refl->RemoveLast(message, field_desc);
    }
}

void goby::apps::zeromq::LiaisonCommander::ControlsContainer::CommandContainer::
    handle_toggle_single_message(const WMouseEvent& mouse, google::protobuf::Message* message,
                                 const google::protobuf::FieldDescriptor* field_desc,
                                 WPushButton* button, WTreeTableNode* parent)
{
    if (button->text() == MESSAGE_INCLUDE_TEXT)
    {
        generate_tree(parent, message->GetReflection()->MutableMessage(message, field_desc));

        parent->expand();

        button->setText(MESSAGE_REMOVE_TEXT);
    }
    else
    {
        const std::vector<WTreeNode*> children = parent->childNodes();
        message->GetReflection()->ClearField(message, field_desc);
        for (int i = 0, n = children.size(); i < n; ++i) parent->removeChildNode(children[i]);

        button->setText(MESSAGE_INCLUDE_TEXT);
    }
}
