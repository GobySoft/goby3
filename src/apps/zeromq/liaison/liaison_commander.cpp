// Copyright 2009-2023:
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

#include <cmath>    // for log10
#include <cstdlib>  // for exit, abs
#include <iostream> // for basic_ostream
#include <iterator> // for ostreambuf_i...
#include <limits>   // for numeric_limits
#include <list>     // for operator!=
#include <memory>   // for shared_ptr
#include <regex>    // for match_result...
#include <sstream>  // for basic_string...
#include <utility>  // for pair, make_pair

#include <Wt/Dbo/Call>                                  // for Call
#include <Wt/Dbo/Exception>                             // for Exception, Wt
#include <Wt/Dbo/FixedSqlConnectionPool>                // for FixedSqlConn...
#include <Wt/Dbo/Query>                                 // for Query
#include <Wt/Dbo/QueryModel>                            // for QueryModel
#include <Wt/Dbo/Transaction>                           // for Transaction
#include <Wt/Dbo/backend/Sqlite3>                       // for Sqlite3
#include <Wt/Dbo/ptr>                                   // for ptr, MetaDbo
#include <Wt/WAbstractItemModel>                        // for WAbstractIte...
#include <Wt/WApplication>                              // for WApplication
#include <Wt/WColor>                                    // for WColor
#include <Wt/WComboBox>                                 // for WComboBox
#include <Wt/WCssDecorationStyle>                       // for WCssDecorati...
#include <Wt/WDialog>                                   // for WDialog, WDi...
#include <Wt/WDoubleValidator>                          // for WDoubleValid...
#include <Wt/WEnvironment>                              // for WEnvironment
#include <Wt/WFlags>                                    // for WFlags
#include <Wt/WFormWidget>                               // for WFormWidget
#include <Wt/WGlobal>                                   // for Right
#include <Wt/WIntValidator>                             // for WIntValidator
#include <Wt/WLabel>                                    // for WLabel
#include <Wt/WLengthValidator>                          // for WLengthValid...
#include <Wt/WLineEdit>                                 // for WLineEdit
#include <Wt/WModelIndex>                               // for WModelIndex
#include <Wt/WRegExpValidator>                          // for WRegExpValid...
#include <Wt/WSignal>                                   // for EventSignal
#include <Wt/WSpinBox>                                  // for WSpinBox
#include <Wt/WStackedWidget>                            // for WStackedWidget
#include <Wt/WStringListModel>                          // for WStringListM...
#include <Wt/WText>                                     // for WText
#include <Wt/WTreeTable>                                // for WTreeTable
#include <Wt/WTreeView>                                 // for WTreeView
#include <Wt/WValidator>                                // for WValidator
#include <Wt/WWidget>                                   // for WWidget
#include <boost/algorithm/string/classification.hpp>    // for is_any_ofF
#include <boost/algorithm/string/split.hpp>             // for split
#include <boost/any.hpp>                                // for any
#include <boost/bind/bind.hpp>                               // for bind_t, list...
#include <boost/cstdint.hpp>                            // for uint32_t
#include <boost/date_time/gregorian/gregorian.hpp>      // for date
#include <boost/date_time/posix_time/posix_time_io.hpp> // for operator<<
#include <boost/date_time/special_defs.hpp>             // for neg_infin
#include <boost/detail/basic_pointerbuf.hpp>            // for basic_pointe...
#include <boost/function.hpp>                           // for function
#include <boost/iterator/iterator_traits.hpp>           // for iterator_val...
#include <boost/lexical_cast/bad_lexical_cast.hpp>      // for bad_lexical_...
#include <boost/operators.hpp>                          // for operator>
#include <boost/signals2/expired_slot.hpp>              // for expired_slot
#include <boost/smart_ptr/shared_ptr.hpp>               // for shared_ptr
#include <dccl/option_extensions.pb.h>                  // for DCCLFieldOpt...
#include <google/protobuf/descriptor.h>                 // for FieldDescriptor
#include <google/protobuf/descriptor.pb.h>              // for FieldOptions
#include <google/protobuf/text_format.h>                // for TextFormat

#include "dccl/dynamic_protobuf_manager.h" // for DynamicProto...
#include "goby/middleware/common.h"        // for to_string
#include "goby/middleware/marshalling/dccl.h"
#include "goby/middleware/protobuf/layer.pb.h"      // for Layer_Parse
#include "goby/middleware/transport/interthread.h"  // for InterThreadT...
#include "goby/middleware/transport/intervehicle.h" // for InterVehicle...
#include "goby/time/types.h"                        // for MicroTime
#include "goby/util/as.h"                           // for as, FLOAT_FIXED
#include "goby/util/binary.h"                       // for hex_encode
#include "goby/util/dccl_compat.h"
#include "goby/util/debug_logger/flex_ostreambuf.h" // for DEBUG1, WARN
#include "liaison_commander.h"

#if GOOGLE_PROTOBUF_VERSION < 3001000
#define ByteSizeLong ByteSize
#endif

namespace Wt
{
class WTreeNode;
} // namespace Wt

using namespace Wt;
using namespace goby::util::logger;
using goby::glog;

std::mutex goby::apps::zeromq::LiaisonCommander::dbo_mutex_;
Dbo::backend::Sqlite3* goby::apps::zeromq::LiaisonCommander::sqlite3_(nullptr);
std::unique_ptr<Dbo::FixedSqlConnectionPool> goby::apps::zeromq::LiaisonCommander::connection_pool_;
boost::posix_time::ptime goby::apps::zeromq::LiaisonCommander::last_db_update_time_(
    goby::time::SystemClock::now<boost::posix_time::ptime>());

const std::string MESSAGE_INCLUDE_TEXT = "include";
const std::string MESSAGE_REMOVE_TEXT = "remove";

const std::string EXTERNAL_DATA_LOAD_TEXT = "load";

const std::string STRIPE_ODD_CLASS = "odd";
const std::string STRIPE_EVEN_CLASS = "even";

goby::apps::zeromq::protobuf::ProtobufCommanderConfig::LoadProtobuf::GroupLayer
to_group_layer(const std::string& group, const std::string& layer)
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
          std::uint32_t groupnum = goby::middleware::Group::invalid_numeric_group)
{
    std::string groupnum_str;

    if (grouplayer.layer() >= goby::middleware::protobuf::LAYER_INTERVEHICLE)
    {
        if (groupnum == goby::middleware::Group::invalid_numeric_group)
        {
            if (grouplayer.has_group_numeric_field_name())
                groupnum_str =
                    std::string("/{value of \"" + grouplayer.group_numeric_field_name() + "\"}");
            else
                groupnum_str = std::string("/") + std::to_string(grouplayer.group_numeric());
        }
        else
        {
            groupnum_str = std::string("/") + std::to_string(groupnum);
        }
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

goby::apps::zeromq::LiaisonCommander::~LiaisonCommander() = default;

void goby::apps::zeromq::LiaisonCommander::display_notify_subscription(
    const std::vector<unsigned char>& data, int /*scheme*/, const std::string& type,
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
    const google::protobuf::Message& pb_msg, const std::string& title,
    const goby::apps::zeromq::protobuf::ProtobufCommanderConfig::NotificationSubscription::Color&
        background_color)
{
    auto* new_div = new WContainerWidget(controls_div_->incoming_message_stack_);
    new_div->setOverflow(OverflowAuto);
    new_div->setMaximumSize(400, 600);

    new WText("Message: " + goby::util::as<std::string>(
                                controls_div_->incoming_message_stack_->children().size()),
              new_div);

    new Wt::WBreak(new_div);

    auto* minus = new WPushButton("-", new_div);
    auto* plus = new WPushButton("+", new_div);
    auto* remove = new WPushButton("x", new_div);
    auto* remove_all = new WPushButton("X", new_div);
    remove_all->setFloatSide(Wt::Right);

    auto* box = new WGroupBox(title, new_div);

    new_div->decorationStyle().setBackgroundColor(Wt::WColor(
        background_color.r(), background_color.g(), background_color.b(), background_color.a()));

    new WText("<pre>" + pb_msg.DebugString() + "</pre>", box);

    plus->clicked().connect(controls_div_, &ControlsContainer::increment_incoming_messages);
    minus->clicked().connect(controls_div_, &ControlsContainer::decrement_incoming_messages);
    remove->clicked().connect(controls_div_, &ControlsContainer::remove_incoming_message);
    remove_all->clicked().connect(controls_div_, &ControlsContainer::clear_incoming_messages);
    controls_div_->incoming_message_stack_->setCurrentIndex(
        controls_div_->incoming_message_stack_->children().size() - 1);
}

void goby::apps::zeromq::LiaisonCommander::ControlsContainer::increment_incoming_messages(
    const WMouseEvent& /*event*/)
{
    int new_index = incoming_message_stack_->currentIndex() + 1;
    if (new_index == static_cast<int>(incoming_message_stack_->children().size()))
        new_index = 0;

    incoming_message_stack_->setCurrentIndex(new_index);
}

void goby::apps::zeromq::LiaisonCommander::ControlsContainer::decrement_incoming_messages(
    const WMouseEvent& /*event*/)
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

void goby::apps::zeromq::LiaisonCommander::ControlsContainer::clear_incoming_messages(
    const WMouseEvent& event)
{
    while (incoming_message_stack_->children().size() > 0) remove_incoming_message(event);
}

void goby::apps::zeromq::LiaisonCommander::loop()
{
    auto* current_command = dynamic_cast<ControlsContainer::CommandContainer*>(
        controls_div_->commands_div_->currentWidget());

    if (current_command && current_command->time_fields_.size())
    {
        for (auto it = current_command->time_fields_.begin(),
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
        current_command->sent_model_->reload();
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
        connection_pool_ = std::make_unique<Dbo::FixedSqlConnectionPool>(
            sqlite3_, pb_commander_config_.database_pool_size());
    }

    {
        std::lock_guard<std::mutex> slock(dbo_mutex_);
        session_.setConnectionPool(*connection_pool_);
        session_.mapClass<CommandEntry>("_liaison_commands");
        session_.mapClass<ExternalData>("_external_data");

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

    Dbo::ptr<CommandEntry> last_command(static_cast<CommandEntry*>(nullptr));
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
                auto* new_command =
                    new CommandContainer(pb_commander_config_, pb_commander_config.load_protobuf(i),
                                         protobuf_name, &session_, commander_, send_button_);

                //master_field_info_stack_);
                commands_div_->addWidget(new_command);
                // index of the newly added widget
                commands_[protobuf_name] = commands_div_->count() - 1;
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

void goby::apps::zeromq::LiaisonCommander::ControlsContainer::CommandContainer::load_groups(
    const google::protobuf::Descriptor* desc)
{
    const auto& protobuf_name = desc->full_name();

    for (const auto& grouplayer : load_config_.publish_to())
    {
        // validate grouplayer
        bool grouplayer_valid = true;

        if (grouplayer.has_group_numeric_field_name())
        {
            auto group_numeric_field = desc->FindFieldByName(grouplayer.group_numeric_field_name());

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
                glog.is(WARN) && glog << "In message " << protobuf_name << ": field named "
                                      << grouplayer.group_numeric_field_name()
                                      << " must be (u)int(32|64) or enum type to use "
                                         "for group numeric value"
                                      << std::endl;
                grouplayer_valid = false;
            }
        }

        if (grouplayer_valid)
        {
            group_selection_->addItem(to_string(grouplayer));
            publish_to_.push_back(grouplayer);
        }
    }
}

void goby::apps::zeromq::LiaisonCommander::ControlsContainer::CommandContainer::load_external_data(
    const google::protobuf::Descriptor* desc)
{
    for (const auto& external_data : load_config_.external_data())
    {
        const google::protobuf::Descriptor* external_desc =
            dccl::DynamicProtobufManager::find_descriptor(external_data.name());

        if (!external_desc)
        {
            glog.is(WARN) &&
                glog << "Could not find protobuf name " << external_data.name()
                     << " to load for external_data in Protobuf Commander (configuration line "
                        "`load_protobuf { external_data { name: } }`). Skipping..."
                     << std::endl;

            continue;
        }

        // avoid multiple subscribe
        if (!external_types_.count(external_desc))
        {
            commander_->post_to_comms([=]() {
                std::regex special_chars{R"([-[\]{}()*+?.,\^$|#\s])"};
                std::string sanitized_type =
                    std::regex_replace(std::string(external_data.name()), special_chars, R"(\$&)");

                auto external_data_callback =
                    [=](const std::shared_ptr<const google::protobuf::Message>& msg,
                        const std::string& type) {
                        commander_->post_to_wt([=]() {
                            this->handle_external_data(type, external_data.group(), msg);
                        });
                    };

                commander_->goby_thread()
                    ->interprocess()
                    .subscribe_type_regex<google::protobuf::Message>(
                        external_data_callback,
                        goby::middleware::DynamicGroup(external_data.group()),
                        "^" + sanitized_type + "$");
            });
            external_types_.insert(external_desc);
        }

        for (const auto& translate : external_data.translate())
        {
            CommandContainer::ExternalDataMeta& meta =
                externally_loadable_fields_["." + translate.to()][external_data.name()];
            meta.pb = external_data;
            meta.external_desc = external_desc;
            std::vector<std::string> from_fields, to_fields;

            // verify that translates are valid
            boost::split(from_fields, translate.from(), boost::is_any_of("."));
            boost::split(to_fields, translate.to(), boost::is_any_of("."));

            auto check_fields = [](const std::vector<std::string>& fields,
                                   const google::protobuf::Descriptor* root_desc) {
                const google::protobuf::FieldDescriptor* field = nullptr;
                const google::protobuf::Descriptor* desc = root_desc;

                for (int i = 0, n = fields.size(); i < n; ++i)
                {
                    field = desc->FindFieldByName(fields[i]);
                    if (!field)
                    {
                        glog.is(DIE) && glog << "Invalid field " << fields[i]
                                             << " for message: " << desc->full_name() << std::endl;
                        // avoid false positive with clang static analyzer
                        exit(EXIT_FAILURE);
                    }

                    // not last field
                    if (i + 1 < n)
                    {
                        if (field->cpp_type() != google::protobuf::FieldDescriptor::CPPTYPE_MESSAGE)
                            glog.is(DIE) && glog << "Field " << fields[i]
                                                 << " is not a message type but '.' syntax is used "
                                                    "suggesting children"
                                                 << std::endl;

                        desc = field->message_type();
                    }
                }
            };

            check_fields(from_fields, external_desc);
            check_fields(to_fields, desc);
        }
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
        auto* current_command = dynamic_cast<CommandContainer*>(commands_div_->currentWidget());
        current_command->message_->Clear();
        current_command->generate_root();
        current_command->check_dynamics();
    }
}

void goby::apps::zeromq::LiaisonCommander::ControlsContainer::send_message()
{
    glog.is(VERBOSE) && glog << "Message to be sent!" << std::endl;

    auto* current_command = dynamic_cast<CommandContainer*>(commands_div_->currentWidget());

    auto grouplayer =
        current_command->publish_to_.at(current_command->group_selection_->currentIndex());
    std::uint32_t group_numeric = grouplayer.group_numeric();

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
                if (val >= std::numeric_limits<std::uint32_t>::min() &&
                    val <= std::numeric_limits<std::uint32_t>::max())
                    group_numeric = val;
                break;
            }

            case google::protobuf::FieldDescriptor::CPPTYPE_UINT32:
            {
                auto val = refl->GetUInt32(*current_command->message_, group_numeric_field_desc);
                if (val <= std::numeric_limits<std::uint32_t>::max())
                    group_numeric = val;

                break;
            }

            case google::protobuf::FieldDescriptor::CPPTYPE_INT64:
            {
                auto val = refl->GetInt64(*current_command->message_, group_numeric_field_desc);
                if (val >= std::numeric_limits<std::uint32_t>::min() &&
                    val <= std::numeric_limits<std::uint32_t>::max())
                    group_numeric = val;

                break;
            }

            case google::protobuf::FieldDescriptor::CPPTYPE_UINT64:
            {
                auto val = refl->GetUInt64(*current_command->message_, group_numeric_field_desc);
                if (val <= std::numeric_limits<std::uint32_t>::max())
                    group_numeric = val;

                break;
            }
            case google::protobuf::FieldDescriptor::CPPTYPE_ENUM:
            {
                auto val =
                    refl->GetEnum(*current_command->message_, group_numeric_field_desc)->number();
                if (val >= std::numeric_limits<std::uint32_t>::min() &&
                    val <= std::numeric_limits<std::uint32_t>::max())
                    group_numeric = val;

                break;
            }
        }
    }

    WDialog dialog("Confirm sending of message: " + command_selection_->currentText());

    auto* comment_box = new WGroupBox("Log comment", dialog.contents());
    auto* comment_line = new WLineEdit(comment_box);
    comment_line->setText(comment_line_->text());

    auto* group_box = new WGroupBox("Group", dialog.contents());
    auto* group_div = new WContainerWidget(group_box);
    new WText("Group: " + to_string(grouplayer, group_numeric), group_div);

    auto* message_box = new WGroupBox("Message to send", dialog.contents());
    auto* message_div = new WContainerWidget(message_box);

    auto message_to_send = current_command->message_;

#if DCCL_VERSION_MAJOR >= 4
    auto desc = current_command->message_->GetDescriptor();
    if (current_command->has_dynamic_conditions_ &&
        desc->options().GetExtension(dccl::msg).has_id())
    {
        // run through DCCL to omit / round fields as needed
        using DCCLHelper = middleware::SerializerParserHelper<google::protobuf::Message,
                                                              middleware::MarshallingScheme::DCCL>;
        std::vector<char> bytes = DCCLHelper::serialize(*message_to_send);
        std::vector<char>::iterator actual_end;
        message_to_send =
            DCCLHelper::parse(bytes.begin(), bytes.end(), actual_end,
                              current_command->message_->GetDescriptor()->full_name());
    }
#endif

    new WText("<pre>" + message_to_send->DebugString() + "</pre>", message_div);

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
                            message_to_send, goby::middleware::DynamicGroup(grouplayer.group()));
                });
                break;
            case goby::middleware::protobuf::LAYER_INTERMODULE:
            case goby::middleware::protobuf::LAYER_INTERPROCESS:
                commander_->post_to_comms([=]() {
                    commander_->goby_thread()
                        ->interprocess()
                        .publish_dynamic<google::protobuf::Message>(
                            message_to_send, goby::middleware::DynamicGroup(grouplayer.group()));
                });
                break;
            case goby::middleware::protobuf::LAYER_INTERVEHICLE:
                commander_->post_to_comms([=]() {
                    commander_->goby_thread()
                        ->intervehicle()
                        .publish_dynamic<google::protobuf::Message,
                                         goby::middleware::MarshallingScheme::DCCL>(
                            message_to_send,
                            goby::middleware::DynamicGroup(grouplayer.group(), group_numeric),
                            commander_->goby_thread()->command_publisher_);
                });
                break;
        }

        auto* command_entry = new CommandEntry;
        command_entry->protobuf_name = message_to_send->GetDescriptor()->full_name();
        command_entry->bytes.resize(message_to_send->ByteSizeLong());
        message_to_send->SerializeToArray(&command_entry->bytes[0], command_entry->bytes.size());
        command_entry->address = wApp->environment().clientAddress();
        command_entry->group = grouplayer.group();
        command_entry->layer = goby::middleware::to_string(grouplayer.layer());

        boost::posix_time::ptime now = goby::time::SystemClock::now<boost::posix_time::ptime>();
        command_entry->time.setPosixTime(now);
        command_entry->utime = current_command->latest_time_;

        command_entry->comment = comment_line->text().narrow();
        if (command_entry->comment.empty())
        {
            command_entry->comment =
                "[" + message_to_send->ShortDebugString().substr(0, 100) + "...]";
        }

        command_entry->last_ack = 0;
        session_.add(command_entry);

        {
            std::lock_guard<std::mutex> slock(dbo_mutex_);
            Dbo::Transaction transaction(*current_command->session_);
            transaction.commit();
            last_db_update_time_ = now;
        }

        comment_line_->setText("");
        current_command->sent_model_->reload();
    }
}

goby::apps::zeromq::LiaisonCommander::ControlsContainer::CommandContainer::CommandContainer(
    const protobuf::ProtobufCommanderConfig& pb_commander_config,
    const protobuf::ProtobufCommanderConfig::LoadProtobuf& load_config,
    const std::string& protobuf_name, Dbo::Session* session, LiaisonCommander* commander,
    Wt::WPushButton* send_button)
    : WGroupBox(protobuf_name),
      message_(dccl::DynamicProtobufManager::new_protobuf_message<
               std::shared_ptr<google::protobuf::Message>>(protobuf_name)),
      latest_time_(0),
      group_div_(new WContainerWidget(this)),
      group_label_(new WLabel("Group: ", group_div_)),
      group_selection_(new WComboBox(group_div_)),
      message_tree_box_(new WGroupBox("Contents", this)),
      message_tree_table_(new WTreeTable(message_tree_box_)),
      //      field_info_stack_(new WStackedWidget(master_field_info_stack)),
      session_(session),
      sent_model_(new Dbo::QueryModel<Dbo::ptr<CommandEntry>>(this)),
      sent_box_(new WGroupBox("Sent message log (click for details)", this)),
      sent_clear_(new WPushButton("Clear", sent_box_)),
      sent_table_(new WTreeView(sent_box_)),
      external_data_model_(new Dbo::QueryModel<Dbo::ptr<ExternalData>>(this)),
      external_data_box_(new WGroupBox("External Data", this)),
      external_data_clear_(new WPushButton("Clear", external_data_box_)),
      external_data_table_(new WTreeView(external_data_box_)),
      last_reload_time_(boost::posix_time::neg_infin),
      pb_commander_config_(pb_commander_config),
      load_config_(load_config),
      commander_(commander),
      send_button_(send_button)
{
    //    new WText("", field_info_stack_);
    //field_info_map_[0] = 0;

    message_tree_table_->addColumn("Value", pb_commander_config.value_width_pixels());
    message_tree_table_->addColumn("Modify", pb_commander_config.modify_width_pixels());
    message_tree_table_->addColumn("External Data",
                                   pb_commander_config.external_data_width_pixels());

    {
        std::lock_guard<std::mutex> slock(dbo_mutex_);
        Dbo::Transaction transaction(*session_);
        sent_model_->setQuery(
            session_->find<CommandEntry>("where protobuf_name='" + protobuf_name + "'"));
    }

    sent_model_->addColumn("comment", "Comment");
    sent_model_->addColumn("protobuf_name", "Name");
    sent_model_->addColumn("group", "Group");
    sent_model_->addColumn("layer", "Layer");
    sent_model_->addColumn("address", "Network Address");
    sent_model_->addColumn("time", "Time");
    //sent_model_->addColumn("last_ack", "Latest Ack");

    sent_table_->setModel(sent_model_);
    sent_table_->resize(WLength::Auto, pb_commander_config.database_view_height());
    sent_table_->sortByColumn(protobuf::ProtobufCommanderConfig::COLUMN_TIME, DescendingOrder);
    sent_table_->setMinimumSize(pb_commander_config.database_width().comment_width() +
                                    pb_commander_config.database_width().name_width() +
                                    pb_commander_config.database_width().group_width() +
                                    pb_commander_config.database_width().layer_width() +
                                    pb_commander_config.database_width().ip_width() +
                                    pb_commander_config.database_width().time_width() +
                                    //pb_commander_config.database_width().last_ack_width() +
                                    7 * (protobuf::ProtobufCommanderConfig::COLUMN_MAX + 1),
                                WLength::Auto);

    sent_table_->setColumnWidth(protobuf::ProtobufCommanderConfig::COLUMN_COMMENT,
                                pb_commander_config.database_width().comment_width());
    sent_table_->setColumnWidth(protobuf::ProtobufCommanderConfig::COLUMN_NAME,
                                pb_commander_config.database_width().name_width());
    sent_table_->setColumnWidth(protobuf::ProtobufCommanderConfig::COLUMN_GROUP,
                                pb_commander_config.database_width().group_width());
    sent_table_->setColumnWidth(protobuf::ProtobufCommanderConfig::COLUMN_LAYER,
                                pb_commander_config.database_width().layer_width());
    sent_table_->setColumnWidth(protobuf::ProtobufCommanderConfig::COLUMN_IP,
                                pb_commander_config.database_width().ip_width());

    sent_table_->setColumnWidth(protobuf::ProtobufCommanderConfig::COLUMN_TIME,
                                pb_commander_config.database_width().time_width());

    //    sent_table_->setColumnWidth(protobuf::ProtobufCommanderConfig::COLUMN_LAST_ACK,
    //                                 pb_commander_config.database_width().last_ack_width());

    sent_table_->clicked().connect(this, &CommandContainer::handle_database_double_click);

    if (sent_model_->rowCount() > 0)
    {
        const Dbo::ptr<CommandEntry>& entry = sent_model_->resultRow(0);
        message_->ParseFromArray(&entry->bytes[0], entry->bytes.size());

        int group_index =
            group_selection_->findText(to_string(to_group_layer(entry->group, entry->layer)));
        if (group_index >= 0)
            group_selection_->setCurrentIndex(group_index);
    }

    glog.is(DEBUG1) && glog << "Sent message model has " << sent_model_->rowCount() << " rows"
                            << std::endl;

    {
        std::lock_guard<std::mutex> slock(dbo_mutex_);
        Dbo::Transaction transaction(*session_);
        external_data_model_->setQuery(
            session_->find<ExternalData>("where affiliated_protobuf_name='" + protobuf_name + "'"));
    }

    boost::function<void(const Wt::WMouseEvent&)> sent_clear_callback =
        [=](const Wt::WMouseEvent& /*event*/) {
            WDialog dialog("Confirm clearing of ALL sent messages for " + protobuf_name);
            WPushButton ok("Clear", dialog.contents());
            WPushButton cancel("Cancel", dialog.contents());

            dialog.rejectWhenEscapePressed();
            ok.clicked().connect(&dialog, &WDialog::accept);
            cancel.clicked().connect(&dialog, &WDialog::reject);

            if (dialog.exec() == WDialog::Accepted)
            {
                {
                    std::lock_guard<std::mutex> slock(dbo_mutex_);
                    Dbo::Transaction transaction(*session_);

                    session_->execute("delete from _liaison_commands where protobuf_name='" +
                                      protobuf_name + "'");
                }

                sent_model_->reload();
            }
        };
    sent_clear_->clicked().connect(boost::bind(sent_clear_callback, boost::placeholders::_1));

    set_external_data_model_params(external_data_model_);

    external_data_table_->setModel(external_data_model_);
    set_external_data_table_params(external_data_table_);

    boost::function<void(const Wt::WMouseEvent&)> external_data_clear_callback =
        [=](const Wt::WMouseEvent& /*event*/) {
            WDialog dialog("Confirm clearing of ALL external data for " + protobuf_name);
            WPushButton ok("Clear", dialog.contents());
            WPushButton cancel("Cancel", dialog.contents());

            dialog.rejectWhenEscapePressed();
            ok.clicked().connect(&dialog, &WDialog::accept);
            cancel.clicked().connect(&dialog, &WDialog::reject);

            if (dialog.exec() == WDialog::Accepted)
            {
                {
                    std::lock_guard<std::mutex> slock(dbo_mutex_);
                    Dbo::Transaction transaction(*session_);

                    session_->execute(
                        "delete from _external_data where affiliated_protobuf_name='" +
                        protobuf_name + "'");
                }

                external_data_model_->reload();
            }
        };
    external_data_clear_->clicked().connect(boost::bind(external_data_clear_callback, boost::placeholders::_1));

    load_groups(message_->GetDescriptor());
    load_external_data(message_->GetDescriptor());

    generate_root();

#if DCCL_VERSION_MAJOR >= 4
    glog.is(DEBUG1) && glog << "has_dynamic_conditions? " << has_dynamic_conditions_ << std::endl;
    check_dynamics();
#endif
}

void goby::apps::zeromq::LiaisonCommander::ControlsContainer::CommandContainer::
    set_external_data_model_params(
        Wt::Dbo::QueryModel<Wt::Dbo::ptr<ExternalData>>* external_data_model)
{
    external_data_model->addColumn("protobuf_name", "Name");
    external_data_model->addColumn("group", "Group");
    external_data_model->addColumn("time", "Time");
    external_data_model->addColumn("value", "Value");
}

void goby::apps::zeromq::LiaisonCommander::ControlsContainer::CommandContainer::
    set_external_data_table_params(Wt::WTreeView* external_data_table)
{
    external_data_table->resize(WLength::Auto, pb_commander_config_.database_view_height());
    external_data_table->sortByColumn(protobuf::ProtobufCommanderConfig::EXTERNAL_DATA_COLUMN_TIME,
                                      DescendingOrder);
    external_data_table->setMinimumSize(
        pb_commander_config_.external_database_width().name_width() +
            pb_commander_config_.external_database_width().group_width() +
            pb_commander_config_.external_database_width().time_width() +
            pb_commander_config_.external_database_width().value_width() +
            7 * (protobuf::ProtobufCommanderConfig::EXTERNAL_DATA_COLUMN_MAX + 1),
        WLength::Auto);

    external_data_table->setColumnWidth(
        protobuf::ProtobufCommanderConfig::EXTERNAL_DATA_COLUMN_NAME,
        pb_commander_config_.external_database_width().name_width());
    external_data_table->setColumnWidth(
        protobuf::ProtobufCommanderConfig::EXTERNAL_DATA_COLUMN_GROUP,
        pb_commander_config_.external_database_width().group_width());
    external_data_table->setColumnWidth(
        protobuf::ProtobufCommanderConfig::EXTERNAL_DATA_COLUMN_TIME,
        pb_commander_config_.external_database_width().time_width());
    external_data_table->setColumnWidth(
        protobuf::ProtobufCommanderConfig::EXTERNAL_DATA_COLUMN_VALUE,
        pb_commander_config_.external_database_width().value_width());
}

void goby::apps::zeromq::LiaisonCommander::ControlsContainer::CommandContainer::
    handle_database_double_click(const WModelIndex& index, const WMouseEvent& /*event*/)
{
    glog.is(DEBUG1) && glog << "clicked: " << index.row() << "," << index.column()
                            << ", is_valid: " << index.isValid() << std::endl;

    if (!index.isValid())
        return;

    const Dbo::ptr<CommandEntry>& entry = sent_model_->resultRow(index.row());

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

    auto* comment_box = new WGroupBox("Log comment", database_dialog_->contents());
    new WText(entry->comment, comment_box);

    auto* contents_div = new WContainerWidget(database_dialog_->contents());
    auto* message_box = new WGroupBox("Message posted to " + group, contents_div);

    auto* message_div = new WContainerWidget(message_box);

    new WText("<pre>" + message->DebugString() + "</pre>", message_div);

    protobuf::NetworkAckSet acks;
    acks.ParseFromArray(&entry->acks[0], entry->acks.size());

    auto* acks_box = new WGroupBox("Acks posted", contents_div);
    auto* acks_div = new WContainerWidget(acks_box);
    new WText("<pre>" + acks.DebugString() + "</pre>", acks_div);

    contents_div->setMaximumSize(pb_commander_config_.modal_dimensions().width(),
                                 pb_commander_config_.modal_dimensions().height());
    contents_div->setOverflow(WContainerWidget::OverflowAuto);

    auto* edit = new WPushButton("Edit (replace)", database_dialog_->contents());
    auto* merge = new WPushButton("Edit (merge)", database_dialog_->contents());
    auto* cancel = new WPushButton("Cancel", database_dialog_->contents());

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
                           const std::shared_ptr<google::protobuf::Message>& message,
                           const std::string& group, const std::string& layer)
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

void goby::apps::zeromq::LiaisonCommander::ControlsContainer::CommandContainer::
    handle_external_data(std::string type, std::string group,
                         const std::shared_ptr<const google::protobuf::Message>& msg)
{
    auto* external_data = new ExternalData;
    external_data->protobuf_name = std::move(type);
    external_data->affiliated_protobuf_name = message_->GetDescriptor()->full_name();
    external_data->group = std::move(group);
    boost::posix_time::ptime now = goby::time::SystemClock::now<boost::posix_time::ptime>();
    external_data->time.setPosixTime(now);

    google::protobuf::TextFormat::Printer printer;
    printer.SetSingleLineMode(true);
    printer.SetUseShortRepeatedPrimitives(true);
    printer.PrintToString(*msg, &external_data->value);

    external_data->bytes.resize(msg->ByteSizeLong());
    msg->SerializeToArray(&external_data->bytes[0], external_data->bytes.size());

    session_->add(external_data);
    {
        std::lock_guard<std::mutex> slock(dbo_mutex_);
        Dbo::Transaction transaction(*session_);
        transaction.commit();
    }

    external_data_model_->reload();
}

void goby::apps::zeromq::LiaisonCommander::ControlsContainer::CommandContainer::generate_root()
{
    glog.is_debug1() && glog << "Generating new root with: " << message_->ShortDebugString()
                             << std::endl;

    const google::protobuf::Descriptor* desc = message_->GetDescriptor();

    // Create and set the root node
    auto* root = new WTreeTableNode(desc->name());
    root->setImagePack("resources/");
    root->setStyleClass(STRIPE_EVEN_CLASS);

    // deletes an existing root
    message_tree_table_->setTreeRoot(root, "Field");

    time_fields_.clear();
    oneof_fields_.clear();

    root->expand();

    skip_dynamic_conditions_update_ = true;
    generate_tree(root, message_.get());
    skip_dynamic_conditions_update_ = false;
}

void goby::apps::zeromq::LiaisonCommander::ControlsContainer::CommandContainer::generate_tree(
    WTreeTableNode* parent, google::protobuf::Message* message, const std::string& parent_hierarchy,
    int index)
{
#if DCCL_VERSION_MAJOR >= 4
    if (has_dynamic_conditions_)
        dccl_dycon_.regenerate(message, message_.get(), index);
#endif

    const google::protobuf::Descriptor* desc = message->GetDescriptor();

    for (int i = 0, n = desc->field_count(); i < n; ++i)
        generate_tree_row(parent, message, desc->field(i), parent_hierarchy);

    std::vector<const google::protobuf::FieldDescriptor*> extensions;
#ifdef DCCL_VERSION_4_1_OR_NEWER
    dccl::DynamicProtobufManager::user_descriptor_pool_call(
        &google::protobuf::DescriptorPool::FindAllExtensions, desc, &extensions);
#else
    dccl::DynamicProtobufManager::user_descriptor_pool().FindAllExtensions(desc, &extensions);
#endif
    google::protobuf::DescriptorPool::generated_pool()->FindAllExtensions(desc, &extensions);
    for (auto& extension : extensions)
        generate_tree_row(parent, message, extension, parent_hierarchy);

    check_initialized();
}

void goby::apps::zeromq::LiaisonCommander::ControlsContainer::CommandContainer::generate_tree_row(
    WTreeTableNode* parent, google::protobuf::Message* message,
    const google::protobuf::FieldDescriptor* field_desc, const std::string& parent_hierarchy)
{
    const google::protobuf::Reflection* refl = message->GetReflection();

    int index = parent->childNodes().size();

    std::string field_name =
        field_desc->is_extension() ? "[" + field_desc->full_name() + "]" : field_desc->name();

    if (field_desc->containing_oneof())
        field_name += " (oneof " + field_desc->containing_oneof()->name() + ")";

    field_name += +": ";

    auto* node = new LiaisonTreeTableNode(field_name, nullptr, parent);

    if ((parent->styleClass() == STRIPE_ODD_CLASS && index % 2) ||
        (parent->styleClass() == STRIPE_EVEN_CLASS && !(index % 2)))
        node->setStyleClass(STRIPE_ODD_CLASS);
    else
        node->setStyleClass(STRIPE_EVEN_CLASS);

    WFormWidget* value_field = nullptr;
    WFormWidget* modify_field = nullptr;
    WFormWidget* external_data_field = nullptr;

    if (field_desc->is_repeated())
    {
        //        WContainerWidget* div = new WContainerWidget;
        //        WLabel* label = new WLabel(": ", div);
        auto* spin_box = new WSpinBox;
        spin_box->setTextSize(3);
        //       label->setBuddy(spin_box);
        spin_box->setRange(0, std::numeric_limits<int>::max());
        spin_box->setSingleStep(1);

        spin_box->valueChanged().connect(boost::bind(&CommandContainer::handle_repeated_size_change,
                                                     this, boost::placeholders::_1, message, field_desc, node,
                                                     parent_hierarchy));

        spin_box->setValue(refl->FieldSize(*message, field_desc));

        handle_repeated_size_change(refl->FieldSize(*message, field_desc), message, field_desc,
                                    node, parent_hierarchy);

        modify_field = spin_box;
    }
    else
    {
        if (field_desc->cpp_type() == google::protobuf::FieldDescriptor::CPPTYPE_MESSAGE)
        {
            bool is_required = field_desc->is_required();

#if DCCL_VERSION_MAJOR >= 4
            dccl_dycon_.set_field(field_desc);
            if (field_desc->options().GetExtension(dccl::field).has_dynamic_conditions())
                has_dynamic_conditions_ = true;

            if (dccl_dycon_.has_required_if() && dccl_dycon_.required())
                is_required = true;
#endif

#if DCCL_VERSION_MAJOR >= 4
            if (dccl_dycon_.has_omit_if() && dccl_dycon_.omit())
                return;
#endif

            if (is_required)
            {
                generate_tree(node, message->GetReflection()->MutableMessage(message, field_desc),
                              parent_hierarchy + "." + field_desc->name());
                node->expand();
            }
            else
            {
                auto* button = new WPushButton(MESSAGE_INCLUDE_TEXT);

                button->clicked().connect(
                    boost::bind(&CommandContainer::handle_toggle_single_message, this, boost::placeholders::_1, message,
                                field_desc, button, node, parent_hierarchy));

                if (refl->HasField(*message, field_desc))
                {
                    parent->expand();
                    handle_toggle_single_message(WMouseEvent(), message, field_desc, button, node,
                                                 parent_hierarchy);
                }

                modify_field = button;
                if (field_desc->containing_oneof())
                    oneof_fields_[message][field_desc->containing_oneof()].push_back(modify_field);
            }
        }
        else
        {
            generate_tree_field(value_field, message, field_desc);
        }
    }

    if (externally_loadable_fields_.count(parent_hierarchy + "." + field_desc->name()))
    {
        auto* button = new WPushButton(EXTERNAL_DATA_LOAD_TEXT);

        button->clicked().connect(boost::bind(&CommandContainer::handle_load_external_data, this,
                                              boost::placeholders::_1, message, field_desc, button, node,
                                              parent_hierarchy));
        external_data_field = button;
    }

    if (value_field)
    {
        node->setColumnWidget(1, value_field);
        if (field_desc->containing_oneof())
            oneof_fields_[message][field_desc->containing_oneof()].push_back(value_field);
    }

    if (modify_field)
    {
        dccl_default_modify_field(modify_field, field_desc);

        generate_field_info_box(modify_field, field_desc);

        node->setColumnWidget(2, modify_field);
    }

    if (external_data_field)
    {
        node->setColumnWidget(3, external_data_field);
    }
}

void goby::apps::zeromq::LiaisonCommander::ControlsContainer::CommandContainer::generate_tree_field(
    WFormWidget*& value_field, google::protobuf::Message* message,
    const google::protobuf::FieldDescriptor* field_desc, int index /*= -1*/)
{
#if DCCL_VERSION_MAJOR >= 4
    if (has_dynamic_conditions_)
        dccl_dycon_.regenerate(message, message_.get(), index);
#endif

    const google::protobuf::Reflection* refl = message->GetReflection();

    switch (field_desc->cpp_type())
    {
        case google::protobuf::FieldDescriptor::CPPTYPE_MESSAGE: break;

        case google::protobuf::FieldDescriptor::CPPTYPE_INT32:
        {
            auto* validator = new WIntValidator;

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
            WIntValidator* validator = nullptr;

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
            auto* validator = new WIntValidator;
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
            WIntValidator* validator = nullptr;

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

            WValidator* validator = nullptr;

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

            auto* validator = new WDoubleValidator;
            validator->setRange(std::numeric_limits<float>::lowest(),
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

            auto* validator = new WDoubleValidator;

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
            strings.emplace_back("true");
            strings.emplace_back("false");

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
                strings.emplace_back(enum_desc->value(i)->name());

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
    generate_field_info_box(Wt::WFormWidget*& /*value_field*/,
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
    for (auto ext_field_desc : extensions)
    {
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
                auto dvalue = goby::util::as<double>(value);

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
    update_oneofs(message, field_desc, field);
    check_initialized();
    check_dynamics();
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

    update_oneofs(message, field_desc, field);
    check_initialized();
    check_dynamics();
}

WLineEdit* goby::apps::zeromq::LiaisonCommander::ControlsContainer::CommandContainer::
    generate_single_line_edit_field(google::protobuf::Message* message,
                                    const google::protobuf::FieldDescriptor* field_desc,
                                    const std::string& current_value,
                                    const std::string& default_value,
                                    WValidator* validator /* = 0*/, int index /*= -1*/)
{
    const google::protobuf::Reflection* refl = message->GetReflection();

    auto* line_edit = new WLineEdit();

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

    line_edit->focussed().connect(
        boost::bind(&CommandContainer::handle_focus_changed, this, line_edit));

    return line_edit;
}

void goby::apps::zeromq::LiaisonCommander::ControlsContainer::CommandContainer::
    handle_focus_changed(Wt::WLineEdit* field)
{
    //    std::cout << "FOCUS: " << field << std::endl;
}

WComboBox*
goby::apps::zeromq::LiaisonCommander::ControlsContainer::CommandContainer::generate_combo_box_field(
    google::protobuf::Message* message, const google::protobuf::FieldDescriptor* field_desc,
    const std::vector<WString>& strings, int current_value, const std::string& default_value,
    int index /*= -1*/)
{
    const google::protobuf::Reflection* refl = message->GetReflection();

    auto* combo_box = new WComboBox;
    auto* model = new WStringListModel(strings, this);

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
    if (auto* line_edit = dynamic_cast<WLineEdit*>(value_field))
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

        // don't update the dynamic fields after each automatic time update
        bool skip = skip_dynamic_conditions_update_;
        skip_dynamic_conditions_update_ = true;
        line_edit->changed().emit();
        skip_dynamic_conditions_update_ = skip;
    }
}

void goby::apps::zeromq::LiaisonCommander::ControlsContainer::CommandContainer::update_oneofs(
    google::protobuf::Message* message, const google::protobuf::FieldDescriptor* field_desc,
    Wt::WFormWidget* changed_field)
{
    if (field_desc->containing_oneof())
    {
        for (auto* field : oneof_fields_[message][field_desc->containing_oneof()])
        {
            // clear all other fields in the oneof
            if (field != changed_field)
            {
                // combo box value field
                if (auto* combo_field = dynamic_cast<Wt::WComboBox*>(field))
                {
                    combo_field->setCurrentIndex(0);
                }
                // embedded message button
                else if (auto* button_field = dynamic_cast<Wt::WPushButton*>(field))
                {
                    if (button_field->text() == MESSAGE_REMOVE_TEXT) // that is, message is included
                    {
                        bool skip = skip_dynamic_conditions_update_;
                        skip_dynamic_conditions_update_ = true;
                        glog.is_debug1() && glog << "Disabling: " << field_desc->full_name()
                                                 << std::endl;
                        button_field->clicked().emit(WMouseEvent());
                        skip_dynamic_conditions_update_ = skip;
                    }
                }
                // any other value field
                else if (field)
                {
                    field->setValueText("");
                }
            }
        }
    }
}

void goby::apps::zeromq::LiaisonCommander::ControlsContainer::CommandContainer::
    dccl_default_value_field(WFormWidget*& value_field,
                             const google::protobuf::FieldDescriptor* field_desc)
{
    const dccl::DCCLFieldOptions& options = field_desc->options().GetExtension(dccl::field);

#if DCCL_VERSION_MAJOR >= 4
    dccl_dycon_.set_field(field_desc);
    if (field_desc->options().GetExtension(dccl::field).has_dynamic_conditions())
        has_dynamic_conditions_ = true;

    if (dccl_dycon_.has_omit_if())
    {
        value_field->setHidden(dccl_dycon_.omit());
    }

#endif

    if (options.has_min() && options.has_max())
    {
        double min = options.min();
        double max = options.max();
        WValidator* validator = value_field->validator();

#if DCCL_VERSION_MAJOR >= 4
        if (dccl_dycon_.has_max())
            max = std::min(max, dccl_dycon_.max());
        if (dccl_dycon_.has_min())
            min = std::max(min, dccl_dycon_.min());

        if (dccl_dycon_.has_required_if())
            validator->setMandatory(field_desc->is_required() || dccl_dycon_.required());
#endif

        if (auto* int_validator = dynamic_cast<WIntValidator*>(validator))
            int_validator->setRange(min, max);
        if (auto* double_validator = dynamic_cast<WDoubleValidator*>(validator))
            double_validator->setRange(min, max);
    }

    if (options.has_static_value())
    {
        if (auto* line_edit = dynamic_cast<WLineEdit*>(value_field))
        {
            line_edit->setText(options.static_value());
            line_edit->changed().emit();
        }

        else if (auto* combo_box = dynamic_cast<WComboBox*>(value_field))
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
            auto* validator = new WLengthValidator(0, options.max_length());
            value_field->setValidator(validator);
        }
        else if (field_desc->type() == google::protobuf::FieldDescriptor::TYPE_BYTES)
        {
            auto* validator =
                new WRegExpValidator("([0-9,a-f,A-F][0-9,a-f,A-F]){0," +
                                     goby::util::as<std::string>(options.max_length()) + "}");

            value_field->setValidator(validator);
        }
    }

    if (options.codec() == "_time" || options.codec() == "dccl.time2" ||
        options.codec() == "dccl.time")
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
        if (auto* spin_box = dynamic_cast<WSpinBox*>(modify_field))
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
                                WTreeTableNode* parent, const std::string& parent_hierarchy)
{
    const google::protobuf::Reflection* refl = message->GetReflection();

    // add nodes
    while (desired_size > static_cast<int>(parent->childNodes().size()))
    {
        int index = parent->childNodes().size();
        auto* node =
            new WTreeTableNode("index: " + goby::util::as<std::string>(index), nullptr, parent);

        if ((parent->styleClass() == STRIPE_ODD_CLASS && index % 2) ||
            (parent->styleClass() == STRIPE_EVEN_CLASS && !(index % 2)))
            node->setStyleClass(STRIPE_ODD_CLASS);
        else
            node->setStyleClass(STRIPE_EVEN_CLASS);

        WFormWidget* value_field = nullptr;

        if (field_desc->cpp_type() == google::protobuf::FieldDescriptor::CPPTYPE_MESSAGE)
        {
            if (refl->FieldSize(*message, field_desc) <= index)
            {
                generate_tree(node, refl->AddMessage(message, field_desc),
                              parent_hierarchy + "." + field_desc->name(), index);
            }
            else
            {
                generate_tree(node, refl->MutableRepeatedMessage(message, field_desc, index),
                              parent_hierarchy + "." + field_desc->name(), index);
                parent->expand();
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

        oneof_fields_.erase(&refl->GetRepeatedMessage(*message, field_desc,
                                                      refl->FieldSize(*message, field_desc) - 1));
        refl->RemoveLast(message, field_desc);
    }

    check_initialized();
    check_dynamics();
}

void goby::apps::zeromq::LiaisonCommander::ControlsContainer::CommandContainer::
    handle_toggle_single_message(const WMouseEvent& /*mouse*/, google::protobuf::Message* message,
                                 const google::protobuf::FieldDescriptor* field_desc,
                                 WPushButton* button, WTreeTableNode* parent,
                                 const std::string& parent_hierarchy)
{
    if (button->text() == MESSAGE_INCLUDE_TEXT)
    {
        parent->expand();
        generate_tree(parent, message->GetReflection()->MutableMessage(message, field_desc),
                      parent_hierarchy + "." + field_desc->name());

        button->setText(MESSAGE_REMOVE_TEXT);
        update_oneofs(message, field_desc, button);
    }
    else
    {
        const std::vector<WTreeNode*> children = parent->childNodes();
        if (message->GetReflection()->HasField(*message, field_desc))
        {
            oneof_fields_.erase(&message->GetReflection()->GetMessage(*message, field_desc));
            message->GetReflection()->ClearField(message, field_desc);
        }

        for (auto i : children) parent->removeChildNode(i);

        button->setText(MESSAGE_INCLUDE_TEXT);
    }

    check_initialized();
    check_dynamics();
}

void goby::apps::zeromq::LiaisonCommander::ControlsContainer::CommandContainer::
    handle_load_external_data(const WMouseEvent& /*mouse*/, google::protobuf::Message* /*message*/,
                              const google::protobuf::FieldDescriptor* field_desc,
                              WPushButton* /*button*/, WTreeTableNode* /*parent*/,
                              const std::string& parent_hierarchy)
{
    WDialog dialog("Available external data for field: " + field_desc->name() +
                   " (click to select)");

    auto* choice_box = new WGroupBox("Choose external data message", dialog.contents());
    auto* choice_div = new WContainerWidget(choice_box);

    auto* external_data_model(new Dbo::QueryModel<Dbo::ptr<ExternalData>>(choice_box));
    auto* external_data_table(new WTreeView(choice_div));

    // ".foo.bar.field"
    std::string hierarchy = parent_hierarchy + "." + field_desc->name();
    {
        std::string external_data_where_clause;

        const auto& externally_loadable = externally_loadable_fields_.at(hierarchy);
        for (auto it = externally_loadable.begin(), end = externally_loadable.end(); it != end;
             ++it)
        {
            if (it != externally_loadable.begin())
                external_data_where_clause += " OR ";
            external_data_where_clause +=
                "protobuf_name='" + it->second.external_desc->full_name() + "'";
        }

        std::lock_guard<std::mutex> slock(dbo_mutex_);
        Dbo::Transaction transaction(*session_);
        auto query =
            session_->find<ExternalData>()
                .where("affiliated_protobuf_name='" + message_->GetDescriptor()->full_name() + "'")
                .where(external_data_where_clause);
        external_data_model->setQuery(query);
    }

    set_external_data_model_params(external_data_model);
    external_data_table->setModel(external_data_model);
    set_external_data_table_params(external_data_table);

    auto* message_box = new WGroupBox("External data to load", dialog.contents());
    auto* message_div = new WContainerWidget(message_box);
    auto* message_text = new WText("", message_div);

    WPushButton ok("Load", dialog.contents());
    WPushButton cancel("Cancel", dialog.contents());
    ok.setDisabled(true);

    std::shared_ptr<google::protobuf::Message> message_to_load;

    boost::function<void(const Wt::WModelIndex&, const Wt::WMouseEvent&)> select_data_callback =
        [&](const Wt::WModelIndex& index, const Wt::WMouseEvent& /*event*/) {
            glog.is(DEBUG1) && glog << "clicked: " << index.row() << "," << index.column()
                                    << ", is_valid: " << index.isValid() << std::endl;

            if (!index.isValid())
                return;

            const Dbo::ptr<ExternalData>& entry = external_data_model->resultRow(index.row());

            message_to_load = dccl::DynamicProtobufManager::new_protobuf_message<
                std::shared_ptr<google::protobuf::Message>>(entry->protobuf_name);
            message_to_load->ParseFromArray(&entry->bytes[0], entry->bytes.size());

            message_text->setText(std::string("<pre>") + message_to_load->DebugString() + "</pre>");
            ok.setDisabled(false);
        };

    external_data_table->clicked().connect(boost::bind(select_data_callback, boost::placeholders::_1, boost::placeholders::_2));

    message_div->setMaximumSize(pb_commander_config_.modal_dimensions().width(),
                                pb_commander_config_.modal_dimensions().height());
    message_div->setOverflow(WContainerWidget::OverflowAuto);

    dialog.rejectWhenEscapePressed();

    ok.clicked().connect(&dialog, &WDialog::accept);
    cancel.clicked().connect(&dialog, &WDialog::reject);

    if (dialog.exec() == WDialog::Accepted)
    {
        // find the appropriate ExternalDataMeta
        const ExternalDataMeta& meta = externally_loadable_fields_.at(hierarchy).at(
            message_to_load->GetDescriptor()->full_name());

        // clear fields except the ones we need
        std::cout << "Running translates from: " << meta.pb.ShortDebugString() << std::endl;

        for (const auto& translate : meta.pb.translate())
        {
            std::deque<std::string> from_fields, to_fields;

            boost::split(from_fields, translate.from(), boost::is_any_of("."));
            boost::split(to_fields, translate.to(), boost::is_any_of("."));

            // clear existing "to" fields
            std::pair<const google::protobuf::FieldDescriptor*,
                      std::vector<google::protobuf::Message*>>
                fully_qualified_to_fields =
                    find_fully_qualified_field({&*message_}, to_fields, true, 0);

            auto write_to_message = [&](const std::string& from_text, int index) {
                std::pair<const google::protobuf::FieldDescriptor*,
                          std::vector<google::protobuf::Message*>>
                    fully_qualified_to_fields =
                        find_fully_qualified_field({&*message_}, to_fields, true, index);

                for (auto* msg : fully_qualified_to_fields.second)
                {
                    const auto* field = fully_qualified_to_fields.first;
                    google::protobuf::TextFormat::ParseFieldValueFromString(from_text, field, msg);
                }
            };

            for (auto* msg : fully_qualified_to_fields.second)
            {
                const auto* field = fully_qualified_to_fields.first;
                const google::protobuf::Reflection* refl = msg->GetReflection();
                refl->ClearField(msg, field);
            }

            std::pair<const google::protobuf::FieldDescriptor*,
                      std::vector<google::protobuf::Message*>>
                fully_qualified_from_fields =
                    find_fully_qualified_field({&*message_to_load}, from_fields);

            const auto* field = fully_qualified_from_fields.first;

            int index = 0;
            for (const auto* msg : fully_qualified_from_fields.second)
            {
                const google::protobuf::Reflection* refl = msg->GetReflection();
                if (!field->is_repeated())
                {
                    std::string text;
                    google::protobuf::TextFormat::PrintFieldValueToString(*msg, field, -1, &text);

                    if (field->cpp_type() == google::protobuf::FieldDescriptor::CPPTYPE_MESSAGE)
                        text = "{ " + text + "}";

                    write_to_message(text, index++);
                }
                else
                {
                    for (int i = 0, n = refl->FieldSize(*msg, field); i < n; ++i)
                    {
                        std::string text;
                        google::protobuf::TextFormat::PrintFieldValueToString(*msg, field, i,
                                                                              &text);
                        if (field->cpp_type() == google::protobuf::FieldDescriptor::CPPTYPE_MESSAGE)
                            text = "{ " + text + "}";

                        write_to_message(text, index++);
                    }
                }
            }
        }

        generate_root();
    }
}

std::pair<const google::protobuf::FieldDescriptor*, std::vector<google::protobuf::Message*>>
goby::apps::zeromq::LiaisonCommander::ControlsContainer::CommandContainer::
    find_fully_qualified_field(std::vector<google::protobuf::Message*> msgs,
                               std::deque<std::string> fields, bool set_field, int set_index)
{
    const google::protobuf::Descriptor* desc = msgs[0]->GetDescriptor();
    const auto* field = desc->FindFieldByName(fields[0]);
    std::vector<google::protobuf::Message*> result_msgs;

    if (fields.size() > 1)
    {
        for (auto* msg : msgs)
        {
            const google::protobuf::Reflection* refl = msg->GetReflection();

            if (!field->is_repeated())
            {
                result_msgs.push_back(refl->MutableMessage(msg, field));
            }
            else
            {
                if (set_field)
                {
                    if (set_index < refl->FieldSize(*msg, field))
                        result_msgs.push_back(refl->MutableRepeatedMessage(msg, field, set_index));

                    else
                        result_msgs.push_back(refl->AddMessage(msg, field));
                }
                else
                {
                    for (int i = 0, n = refl->FieldSize(*msg, field); i < n; ++i)
                        result_msgs.push_back(refl->MutableRepeatedMessage(msg, field, i));
                }
            }
        }

        fields.pop_front();
        return find_fully_qualified_field(result_msgs, fields, set_field, set_index);
    }
    else
    {
        return std::make_pair(field, msgs);
    }
}
