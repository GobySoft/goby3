// Copyright 2011-2020:
//   GobySoft, LLC (2013-)
//   Massachusetts Institute of Technology (2007-2014)
//   Community contributors (see AUTHORS file)
// File authors:
//   Toby Schneider <toby@gobysoft.org>
//   Shawn Dooley <shawn@shawndooley.net>
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

#ifndef GOBY_MOOS_GOBY_MOOS_APP_H
#define GOBY_MOOS_GOBY_MOOS_APP_H

#include <map>

#include <boost/algorithm/string.hpp>
#include <boost/bind.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/filesystem.hpp>
#include <boost/function.hpp>
#include <boost/signals2.hpp>
#include <memory>

#include "dynamic_moos_vars.h"
#include "goby/exception.h"
#include "goby/middleware/application/configuration_reader.h"
#include "goby/moos/moos_header.h"
#include "goby/moos/moos_translator.h"
#include "goby/moos/protobuf/goby_moos_app.pb.h"
#include "goby/time.h"
#include "goby/util/as.h"
#include "goby/util/debug_logger.h"
#include "moos_protobuf_helpers.h"

namespace goby
{
namespace moos
{
void write_version_message();

template <typename App> int run(int argc, char* argv[]);

template <typename ProtobufMessage>
inline void protobuf_inbox(const CMOOSMsg& msg,
                           boost::function<void(const ProtobufMessage& msg)> handler)
{
    ProtobufMessage pb_msg;
    parse_for_moos(msg.GetString(), &pb_msg);
    handler(pb_msg);
}

// shell implementation so we can call superclass methods for when
// using AppCastingMOOSApp
class MOOSAppShell : public CMOOSApp
{
  protected:
    bool Iterate() { return true; }
    bool OnStartUp() { return true; }
    bool OnConnectToServer() { return true; }
    bool OnNewMail(MOOSMSG_LIST& NewMail) { return true; }
    void RegisterVariables() {}
    void PostReport() {}
};

template <class MOOSAppType = MOOSAppShell> class GobyMOOSAppSelector : public MOOSAppType
{
  protected:
    typedef boost::function<void(const CMOOSMsg& msg)> InboxFunc;

    template <typename ProtobufConfig>
    explicit GobyMOOSAppSelector(ProtobufConfig* cfg)
        : start_time_(MOOSTime()),
          configuration_read_(false),
          cout_cleared_(false),
          connected_(false),
          started_up_(false),
          ignore_stale_(true),
          dynamic_moos_vars_enabled_(true)
    {
        using goby::glog;

        read_configuration(cfg);

        // keep a copy for ourselves
        common_cfg_ = cfg->common();
        configuration_read_ = true;

        process_configuration();

        glog.is(goby::util::logger::DEBUG2) && glog << cfg->DebugString() << std::endl;
    }

    virtual ~GobyMOOSAppSelector() {}

    template <typename ProtobufMessage>
    void publish_pb(const std::string& key, const ProtobufMessage& msg)
    {
        std::string serialized;
        bool is_binary = serialize_for_moos(&serialized, msg);
        CMOOSMsg moos_msg = goby::moos::MOOSTranslator::make_moos_msg(
            key, serialized, is_binary, goby::moos::moos_technique,
            msg.GetDescriptor()->full_name());
        publish(moos_msg);
    }

    void publish(CMOOSMsg& msg)
    {
        if (connected_ && started_up_)
            MOOSAppType::m_Comms.Post(msg);
        else
            msg_buffer_.push_back(msg);
    }

    void publish(const std::string& key, const std::string& value)
    {
        CMOOSMsg msg(MOOS_NOTIFY, key, value);
        publish(msg);
    }

    void publish(const std::string& key, double value)
    {
        CMOOSMsg msg(MOOS_NOTIFY, key, value);
        publish(msg);
    }

    goby::moos::DynamicMOOSVars& dynamic_vars() { return dynamic_vars_; }
    double start_time() const { return start_time_; }

    void subscribe(const std::string& var, InboxFunc handler = InboxFunc(), int blackout = 0);

    template <typename V, typename A1>
    void subscribe(const std::string& var, void (V::*mem_func)(A1), V* obj, int blackout = 0)
    {
        subscribe(var, boost::bind(mem_func, obj, _1), blackout);
    }

    // wildcard
    void subscribe(const std::string& var_pattern, const std::string& app_pattern,
                   InboxFunc handler = InboxFunc(), int blackout = 0);

    template <typename V, typename A1>
    void subscribe(const std::string& var_pattern, const std::string& app_pattern,
                   void (V::*mem_func)(A1), V* obj, int blackout = 0)
    {
        subscribe(var_pattern, app_pattern, boost::bind(mem_func, obj, _1), blackout);
    }

    template <typename V, typename ProtobufMessage>
    void subscribe_pb(const std::string& var, void (V::*mem_func)(const ProtobufMessage&), V* obj,
                      int blackout = 0)
    {
        subscribe_pb<ProtobufMessage>(var, boost::bind(mem_func, obj, _1), blackout);
    }

    template <typename ProtobufMessage>
    void subscribe_pb(const std::string& var,
                      boost::function<void(const ProtobufMessage& msg)> handler, int blackout = 0)
    {
        subscribe(var, boost::bind(&goby::moos::protobuf_inbox<ProtobufMessage>, _1, handler),
                  blackout);
    }

    void register_timer(int period_seconds, boost::function<void()> handler)
    {
        int now = (goby::time::SystemClock::now<goby::time::SITime>() / boost::units::si::seconds) /
                  period_seconds;
        now *= period_seconds;

        SynchronousLoop new_loop;
        new_loop.unix_next = now + period_seconds;
        new_loop.period_seconds = period_seconds;
        new_loop.handler = handler;
        synchronous_loops_.push_back(new_loop);
    }

    template <typename V> void register_timer(int period_seconds, void (V::*mem_func)(), V* obj)
    {
        register_timer(period_seconds, boost::bind(mem_func, obj));
    }

    template <typename App> friend int ::goby::moos::run(int argc, char* argv[]);

    virtual void loop() = 0;

    bool ignore_stale() { return ignore_stale_; }
    void set_ignore_stale(bool b) { ignore_stale_ = b; }

    bool dynamic_moos_vars_enabled() { return dynamic_moos_vars_enabled_; }
    void set_dynamic_moos_vars_enabled(bool b) { dynamic_moos_vars_enabled_ = b; }

    std::pair<std::string, goby::moos::protobuf::TranslatorEntry::ParserSerializerTechnique>
    parse_type_technique(const std::string& type_and_technique)
    {
        std::string protobuf_type;
        goby::moos::protobuf::TranslatorEntry::ParserSerializerTechnique technique;
        if (!type_and_technique.empty())
        {
            std::string::size_type colon_pos = type_and_technique.find(':');

            if (colon_pos != std::string::npos)
            {
                protobuf_type = type_and_technique.substr(0, colon_pos);
                std::string str_technique = type_and_technique.substr(colon_pos + 1);

                if (!goby::moos::protobuf::TranslatorEntry::ParserSerializerTechnique_Parse(
                        str_technique, &technique))
                    throw(std::runtime_error("Invalid technique string"));
            }
            else
            {
                throw std::runtime_error("Missing colon (:)");
            }
            return std::make_pair(protobuf_type, technique);
        }
        else
        {
            throw std::runtime_error("Empty technique string");
        }
    }

  private:
    // from CMOOSApp
    bool Iterate();
    bool OnStartUp();
    bool OnConnectToServer();
    bool OnDisconnectFromServer();
    bool OnNewMail(MOOSMSG_LIST& NewMail);
    void try_subscribing();
    void do_subscriptions();

    int fetch_moos_globals(google::protobuf::Message* msg, CMOOSFileReader& moos_file_reader);

    void read_configuration(google::protobuf::Message* cfg);
    void process_configuration();

  private:
    // when we started (seconds since UNIX)
    double start_time_;

    // have we read the configuration file fully?
    bool configuration_read_;
    bool cout_cleared_;

    std::ofstream fout_;

    // allows direct reading of newest publish to a given MOOS variable
    goby::moos::DynamicMOOSVars dynamic_vars_;

    std::map<std::string, std::shared_ptr<boost::signals2::signal<void(const CMOOSMsg& msg)> > >
        mail_handlers_;

    std::map<std::pair<std::string, std::string>,
             std::shared_ptr<boost::signals2::signal<void(const CMOOSMsg& msg)> > >
        wildcard_mail_handlers_;

    // CMOOSApp::OnConnectToServer()
    bool connected_;
    // CMOOSApp::OnStartUp()
    bool started_up_;

    std::deque<CMOOSMsg> msg_buffer_;

    // MOOS Variable name, blackout time
    std::deque<std::pair<std::string, int> > pending_subscriptions_;
    std::deque<std::pair<std::string, int> > existing_subscriptions_;

    // MOOS Variable pattern, MOOS App pattern, blackout time
    std::deque<std::pair<std::pair<std::string, std::string>, int> >
        wildcard_pending_subscriptions_;
    std::deque<std::pair<std::pair<std::string, std::string>, int> >
        wildcard_existing_subscriptions_;

    struct SynchronousLoop
    {
        double unix_next;
        int period_seconds;
        boost::function<void()> handler;
    };

    std::vector<SynchronousLoop> synchronous_loops_;

    protobuf::GobyMOOSAppConfig common_cfg_;

    bool ignore_stale_;

    bool dynamic_moos_vars_enabled_;

    static int argc_;
    static char** argv_;
    static std::string mission_file_;
    static std::string application_name_;
};

class GobyMOOSApp : public GobyMOOSAppSelector<>
{
  public:
    template <typename ProtobufConfig>
    explicit GobyMOOSApp(ProtobufConfig* cfg) : GobyMOOSAppSelector<>(cfg)
    {
    }
};
} // namespace moos
} // namespace goby

template <class MOOSAppType>
std::string goby::moos::GobyMOOSAppSelector<MOOSAppType>::mission_file_;

template <class MOOSAppType>
std::string goby::moos::GobyMOOSAppSelector<MOOSAppType>::application_name_;

template <class MOOSAppType> int goby::moos::GobyMOOSAppSelector<MOOSAppType>::argc_ = 0;
template <class MOOSAppType> char** goby::moos::GobyMOOSAppSelector<MOOSAppType>::argv_ = 0;

template <class MOOSAppType> bool goby::moos::GobyMOOSAppSelector<MOOSAppType>::Iterate()
{
    MOOSAppType::Iterate();

    if (!configuration_read_)
        return true;

    // clear out MOOSApp cout for ncurses "scope" mode
    // MOOS has stopped talking by first Iterate()
    if (!cout_cleared_)
    {
        goby::glog.refresh();
        cout_cleared_ = true;
    }

    while (!msg_buffer_.empty() && (connected_ && started_up_))
    {
        goby::glog.is(goby::util::logger::DEBUG3) &&
            goby::glog << "writing from buffer: " << msg_buffer_.front().GetKey() << ": "
                       << msg_buffer_.front().GetAsString() << std::endl;

        MOOSAppType::m_Comms.Post(msg_buffer_.front());
        msg_buffer_.pop_front();
    }

    loop();

    if (synchronous_loops_.size())
    {
        double now = goby::time::SystemClock::now<goby::time::SITime>() / boost::units::si::seconds;
        for (typename std::vector<SynchronousLoop>::iterator it = synchronous_loops_.begin(),
                                                             end = synchronous_loops_.end();
             it != end; ++it)
        {
            SynchronousLoop& loop = *it;
            if (loop.unix_next <= now)
            {
                loop.handler();
                loop.unix_next += loop.period_seconds;

                // fix jumps forward in time
                if (loop.unix_next < now)
                    loop.unix_next = now + loop.period_seconds;
            }

            // fix jumps backwards in time
            if (loop.unix_next > (now + 2 * loop.period_seconds))
                loop.unix_next = now + loop.period_seconds;
        }
    }

    return true;
}

template <class MOOSAppType>
bool goby::moos::GobyMOOSAppSelector<MOOSAppType>::OnNewMail(MOOSMSG_LIST& NewMail)
{
    // for AppCasting (otherwise no-op)
    MOOSAppType::OnNewMail(NewMail);

    for (const auto& msg : NewMail)
    {
        goby::glog.is(goby::util::logger::DEBUG3) &&
            goby::glog << "Received mail: " << msg.GetKey() << ", time: " << std::setprecision(15)
                       << msg.GetTime() << std::endl;

        // update dynamic moos variables - do this inside the loop so the newest is
        // also the one referenced in the call to inbox()
        if (dynamic_moos_vars_enabled_)
            dynamic_vars().update_moos_vars(msg);

        if (msg.GetTime() < start_time_ && ignore_stale_)
        {
            goby::glog.is(goby::util::logger::WARN) &&
                goby::glog << "ignoring normal mail from " << msg.GetKey()
                           << " from before we started (dynamics still updated)" << std::endl;
        }
        else if (mail_handlers_.count(msg.GetKey()))
            (*mail_handlers_[msg.GetKey()])(msg);

        for (auto& wildcard_mail_handler : wildcard_mail_handlers_)
        {
            if (MOOSWildCmp(wildcard_mail_handler.first.first, msg.GetKey()) &&
                MOOSWildCmp(wildcard_mail_handler.first.second, msg.GetSource()))
                (*(wildcard_mail_handler.second))(msg);
        }
    }

    return true;
}

template <class MOOSAppType>
bool goby::moos::GobyMOOSAppSelector<MOOSAppType>::OnDisconnectFromServer()
{
    std::cout << MOOSAppType::m_MissionReader.GetAppName() << ", disconnected from server."
              << std::endl;
    connected_ = false;
    pending_subscriptions_.insert(pending_subscriptions_.end(), existing_subscriptions_.begin(),
                                  existing_subscriptions_.end());
    existing_subscriptions_.clear();
    wildcard_pending_subscriptions_.insert(wildcard_pending_subscriptions_.end(),
                                           wildcard_existing_subscriptions_.begin(),
                                           wildcard_existing_subscriptions_.end());
    wildcard_existing_subscriptions_.clear();
    return true;
}

template <class MOOSAppType> bool goby::moos::GobyMOOSAppSelector<MOOSAppType>::OnConnectToServer()
{
    std::cout << MOOSAppType::m_MissionReader.GetAppName() << ", connected to server." << std::endl;
    connected_ = true;
    try_subscribing();

    for (const auto& ini : common_cfg_.initializer())
    {
        if (ini.has_global_cfg_var())
        {
            std::string result;
            if (MOOSAppType::m_MissionReader.GetValue(ini.global_cfg_var(), result))
            {
                if (ini.type() == protobuf::GobyMOOSAppConfig::Initializer::INI_DOUBLE)
                    publish(ini.moos_var(), goby::util::as<double>(result));
                else if (ini.type() == protobuf::GobyMOOSAppConfig::Initializer::INI_STRING)
                    publish(ini.moos_var(), ini.trim() ? boost::trim_copy(result) : result);
            }
        }
        else
        {
            if (ini.type() == protobuf::GobyMOOSAppConfig::Initializer::INI_DOUBLE)
                publish(ini.moos_var(), ini.dval());
            else if (ini.type() == protobuf::GobyMOOSAppConfig::Initializer::INI_STRING)
                publish(ini.moos_var(), ini.trim() ? boost::trim_copy(ini.sval()) : ini.sval());
        }
    }

    return true;
}

template <class MOOSAppType> bool goby::moos::GobyMOOSAppSelector<MOOSAppType>::OnStartUp()
{
    MOOSAppType::OnStartUp();

    std::cout << MOOSAppType::m_MissionReader.GetAppName() << ", starting ..." << std::endl;
    CMOOSApp::SetCommsFreq(common_cfg_.comm_tick());
    CMOOSApp::SetAppFreq(common_cfg_.app_tick());
    started_up_ = true;
    try_subscribing();
    return true;
}

template <class MOOSAppType>
void goby::moos::GobyMOOSAppSelector<MOOSAppType>::subscribe(const std::string& var,
                                                             InboxFunc handler,
                                                             int blackout /* = 0 */)
{
    goby::glog.is(goby::util::logger::VERBOSE) &&
        goby::glog << "subscribing for MOOS variable: " << var << " @ " << blackout << std::endl;

    pending_subscriptions_.push_back(std::make_pair(var, blackout));
    try_subscribing();

    if (!mail_handlers_[var])
        mail_handlers_[var].reset(new boost::signals2::signal<void(const CMOOSMsg& msg)>);

    if (handler)
        mail_handlers_[var]->connect(handler);
}

template <class MOOSAppType>
void goby::moos::GobyMOOSAppSelector<MOOSAppType>::subscribe(const std::string& var_pattern,
                                                             const std::string& app_pattern,
                                                             InboxFunc handler,
                                                             int blackout /* = 0 */)
{
    goby::glog.is(goby::util::logger::VERBOSE) &&
        goby::glog << "wildcard subscribing for MOOS variable pattern: " << var_pattern
                   << ", app pattern: " << app_pattern << " @ " << blackout << std::endl;

    std::pair<std::string, std::string> key = std::make_pair(var_pattern, app_pattern);
    wildcard_pending_subscriptions_.push_back(std::make_pair(key, blackout));
    try_subscribing();

    if (!wildcard_mail_handlers_.count(key))
        wildcard_mail_handlers_.insert(std::make_pair(
            key, std::make_shared<boost::signals2::signal<void(const CMOOSMsg& msg)>>()));

    if (handler)
        wildcard_mail_handlers_[key]->connect(handler);
}

template <class MOOSAppType> void goby::moos::GobyMOOSAppSelector<MOOSAppType>::try_subscribing()
{
    if (connected_ && started_up_)
        do_subscriptions();
}

template <class MOOSAppType> void goby::moos::GobyMOOSAppSelector<MOOSAppType>::do_subscriptions()
{
    MOOSAppType::RegisterVariables();

    while (!pending_subscriptions_.empty())
    {
        // variable name, blackout
        if (MOOSAppType::m_Comms.Register(pending_subscriptions_.front().first,
                                          pending_subscriptions_.front().second))
        {
            goby::glog.is(goby::util::logger::VERBOSE) &&
                goby::glog << "subscribed for: " << pending_subscriptions_.front().first
                           << std::endl;
        }
        else
        {
            goby::glog.is(goby::util::logger::WARN) &&
                goby::glog << "failed to subscribe for: " << pending_subscriptions_.front().first
                           << std::endl;
        }
        existing_subscriptions_.push_back(pending_subscriptions_.front());
        pending_subscriptions_.pop_front();
    }

    while (!wildcard_pending_subscriptions_.empty())
    {
        // variable name, blackout
        if (MOOSAppType::m_Comms.Register(wildcard_pending_subscriptions_.front().first.first,
                                          wildcard_pending_subscriptions_.front().first.second,
                                          wildcard_pending_subscriptions_.front().second))
        {
            goby::glog.is(goby::util::logger::VERBOSE) &&
                goby::glog << "subscribed for: "
                           << wildcard_pending_subscriptions_.front().first.first << ":"
                           << wildcard_pending_subscriptions_.front().first.second << std::endl;
        }
        else
        {
            goby::glog.is(goby::util::logger::WARN) &&
                goby::glog << "failed to subscribe for: "
                           << wildcard_pending_subscriptions_.front().first.first << ":"
                           << wildcard_pending_subscriptions_.front().first.second << std::endl;
        }

        wildcard_existing_subscriptions_.push_back(wildcard_pending_subscriptions_.front());
        wildcard_pending_subscriptions_.pop_front();
    }
}

template <class MOOSAppType>
int goby::moos::GobyMOOSAppSelector<MOOSAppType>::fetch_moos_globals(
    google::protobuf::Message* msg, CMOOSFileReader& moos_file_reader)
{
    int globals = 0;
    const google::protobuf::Descriptor* desc = msg->GetDescriptor();
    const google::protobuf::Reflection* refl = msg->GetReflection();

    for (int i = 0, n = desc->field_count(); i < n; ++i)
    {
        const google::protobuf::FieldDescriptor* field_desc = desc->field(i);
        if (field_desc->is_repeated())
            continue;

        std::string moos_global = field_desc->options().GetExtension(goby::field).moos_global();

        switch (field_desc->cpp_type())
        {
            case google::protobuf::FieldDescriptor::CPPTYPE_MESSAGE:
            {
                bool message_was_empty = !refl->HasField(*msg, field_desc);
                int set_globals =
                    fetch_moos_globals(refl->MutableMessage(msg, field_desc), moos_file_reader);
                if (set_globals == 0 && message_was_empty)
                    refl->ClearField(msg, field_desc);

                break;
            }

            case google::protobuf::FieldDescriptor::CPPTYPE_INT32:
            {
                int result;
                if (moos_file_reader.GetValue(moos_global, result))
                {
                    refl->SetInt32(msg, field_desc, result);
                    ++globals;
                }

                break;
            }

            case google::protobuf::FieldDescriptor::CPPTYPE_INT64:
            {
                int result;
                if (moos_file_reader.GetValue(moos_global, result))
                {
                    refl->SetInt64(msg, field_desc, result);
                    ++globals;
                }
                break;
            }

            case google::protobuf::FieldDescriptor::CPPTYPE_UINT32:
            {
                unsigned result;
                if (moos_file_reader.GetValue(moos_global, result))
                {
                    refl->SetUInt32(msg, field_desc, result);
                    ++globals;
                }

                break;
            }

            case google::protobuf::FieldDescriptor::CPPTYPE_UINT64:
            {
                unsigned result;
                if (moos_file_reader.GetValue(moos_global, result))
                {
                    refl->SetUInt64(msg, field_desc, result);
                    ++globals;
                }
                break;
            }

            case google::protobuf::FieldDescriptor::CPPTYPE_BOOL:
            {
                bool result;
                if (moos_file_reader.GetValue(moos_global, result))
                {
                    refl->SetBool(msg, field_desc, result);
                    ++globals;
                }
                break;
            }

            case google::protobuf::FieldDescriptor::CPPTYPE_STRING:
            {
                std::string result;
                if (moos_file_reader.GetValue(moos_global, result))
                {
                    refl->SetString(msg, field_desc, result);
                    ++globals;
                }

                break;
            }

            case google::protobuf::FieldDescriptor::CPPTYPE_FLOAT:
            {
                float result;
                if (moos_file_reader.GetValue(moos_global, result))
                {
                    refl->SetFloat(msg, field_desc, result);
                    ++globals;
                }

                break;
            }

            case google::protobuf::FieldDescriptor::CPPTYPE_DOUBLE:
            {
                double result;
                if (moos_file_reader.GetValue(moos_global, result))
                {
                    refl->SetDouble(msg, field_desc, result);
                    ++globals;
                }
                break;
            }

            case google::protobuf::FieldDescriptor::CPPTYPE_ENUM:
            {
                std::string result;
                if (moos_file_reader.GetValue(moos_global, result))
                {
                    const google::protobuf::EnumValueDescriptor* enum_desc =
                        refl->GetEnum(*msg, field_desc)->type()->FindValueByName(result);
                    if (!enum_desc)
                        throw(std::runtime_error(std::string("invalid enumeration " + result +
                                                             " for field " + field_desc->name())));

                    refl->SetEnum(msg, field_desc, enum_desc);
                    ++globals;
                }
                break;
            }
        }
    }
    return globals;
}

template <class MOOSAppType>
void goby::moos::GobyMOOSAppSelector<MOOSAppType>::read_configuration(
    google::protobuf::Message* cfg)
{
    boost::filesystem::path launch_path(argv_[0]);

#if BOOST_FILESYSTEM_VERSION == 3
    application_name_ = launch_path.filename().string();
#else
    application_name_ = launch_path.filename();
#endif

    //
    // READ CONFIGURATION
    //

    boost::program_options::options_description od_all;
    boost::program_options::variables_map var_map;
    try
    {
        boost::program_options::options_description od_cli_only("Given on command line only");
        od_cli_only.add_options()("help,h", "writes this help message")(
            "moos_file,c", boost::program_options::value<std::string>(&mission_file_),
            "path to .moos file")("moos_name,a",
                                  boost::program_options::value<std::string>(&application_name_),
                                  "name to register with MOOS")(
            "example_config,e", "writes an example .moos ProcessConfig block")(
            "version,V", "writes the current version");

        boost::program_options::options_description od_both(
            "Typically given in the .moos file, but may be specified on the command line");

        goby::middleware::ConfigReader::get_protobuf_program_options(od_both, cfg->GetDescriptor());
        od_all.add(od_both);
        od_all.add(od_cli_only);

        boost::program_options::positional_options_description p;
        p.add("moos_file", 1);
        p.add("moos_name", 2);

        boost::program_options::store(boost::program_options::command_line_parser(argc_, argv_)
                                          .options(od_all)
                                          .positional(p)
                                          .run(),
                                      var_map);

        boost::program_options::notify(var_map);

        if (var_map.count("help"))
        {
            std::cerr << od_all << "\n";
            exit(EXIT_SUCCESS);
        }
        else if (var_map.count("example_config"))
        {
            std::cout << "ProcessConfig = " << application_name_ << "\n{";
            goby::middleware::ConfigReader::get_example_cfg_file(cfg, &std::cout, "  ");
            std::cout << "}" << std::endl;
            exit(EXIT_SUCCESS);
        }
        else if (var_map.count("version"))
        {
            goby::moos::write_version_message();
            exit(EXIT_SUCCESS);
        }

        goby::glog.set_name(application_name_);
        goby::glog.add_stream(goby::util::protobuf::GLogConfig::VERBOSE, &std::cout);

        std::string protobuf_text;
        std::ifstream fin;
        fin.open(mission_file_.c_str());
        if (fin.is_open())
        {
            std::string line;
            bool in_process_config = false;
            while (getline(fin, line))
            {
                std::string no_blanks_line = boost::algorithm::erase_all_copy(line, " ");
                if (boost::algorithm::iequals(no_blanks_line, "PROCESSCONFIG=" + application_name_))
                {
                    in_process_config = true;
                }
                else if (in_process_config &&
                         !boost::algorithm::ifind_first(line, "PROCESSCONFIG").empty())
                {
                    break;
                }

                if (in_process_config)
                    protobuf_text += line + "\n";
            }

            if (!in_process_config)
            {
                goby::glog.is(goby::util::logger::DIE) &&
                    goby::glog << "no ProcessConfig block for " << application_name_ << std::endl;
            }

            // trim off "ProcessConfig = __ {"
            protobuf_text.erase(0, protobuf_text.find_first_of('{') + 1);

            // trim off last "}" and anything that follows
            protobuf_text.erase(protobuf_text.find_last_of('}'));

            // convert "//" to "#" for comments
            boost::algorithm::replace_all(protobuf_text, "//", "#");

            google::protobuf::TextFormat::Parser parser;
            goby::util::FlexOStreamErrorCollector error_collector(protobuf_text);
            parser.RecordErrorsTo(&error_collector);
            parser.AllowPartialMessage(true);
            parser.ParseFromString(protobuf_text, cfg);
            if (error_collector.has_errors() || error_collector.has_warnings())
            {
                goby::glog.is(goby::util::logger::DIE) &&
                    goby::glog << "fatal configuration errors (see above)" << std::endl;
            }
        }
        else
        {
            goby::glog.is(goby::util::logger::WARN) && goby::glog << "failed to open "
                                                                  << mission_file_ << std::endl;
        }

        fin.close();

        CMOOSFileReader moos_file_reader;
        moos_file_reader.SetFile(mission_file_);
        fetch_moos_globals(cfg, moos_file_reader);

        // add / overwrite any options that are specified in the cfg file with those given on the command line
        for (const auto& p : var_map)
        {
            // let protobuf deal with the defaults
            if (!p.second.defaulted())
                goby::middleware::ConfigReader::set_protobuf_program_option(var_map, *cfg, p.first,
                                                                            p.second);
        }

        // now the proto message must have all required fields
        if (!cfg->IsInitialized())
        {
            std::vector<std::string> errors;
            cfg->FindInitializationErrors(&errors);

            std::stringstream err_msg;
            err_msg << "Configuration is missing required parameters: \n";
            for (const std::string& s : errors)
                err_msg << goby::util::esc_red << s << "\n" << goby::util::esc_nocolor;

            err_msg << "Make sure you specified a proper .moos file";
            throw(goby::middleware::ConfigException(err_msg.str()));
        }
    }
    catch (goby::middleware::ConfigException& e)
    {
        // output all the available command line options
        std::cerr << od_all << "\n";
        std::cerr << "Problem parsing command-line configuration: \n" << e.what() << "\n";

        throw;
    }
}

template <class MOOSAppType>
void goby::moos::GobyMOOSAppSelector<MOOSAppType>::process_configuration()
{
    //
    // PROCESS CONFIGURATION
    //
    goby::glog.add_stream(common_cfg_.verbosity(), &std::cout);
    if (common_cfg_.show_gui())
    {
        goby::glog.enable_gui();
    }

    if (common_cfg_.log())
    {
        if (!common_cfg_.has_log_path())
        {
            goby::glog.is(goby::util::logger::WARN) &&
                goby::glog << "logging all terminal output to default directory ("
                           << common_cfg_.log_path() << ")."
                           << "set log_path for another path " << std::endl;
        }

        if (!common_cfg_.log_path().empty())
        {
            using namespace boost::posix_time;
            std::string file_name_base = boost::replace_all_copy(application_name_, "/", "_") +
                                         "_" + common_cfg_.community();

            std::string file_name =
                file_name_base + "_" + to_iso_string(second_clock::universal_time()) + ".txt";

            std::string file_symlink = file_name_base + "_latest.txt";

            goby::glog.is(goby::util::logger::VERBOSE) &&
                goby::glog << "logging output to file: " << file_name << std::endl;

            fout_.open(std::string(common_cfg_.log_path() + "/" + file_name).c_str());

            // symlink to "latest.txt"
            remove(std::string(common_cfg_.log_path() + "/" + file_symlink).c_str());
            symlink(file_name.c_str(),
                    std::string(common_cfg_.log_path() + "/" + file_symlink).c_str());

            // if fails, try logging to this directory
            if (!fout_.is_open())
            {
                fout_.open(std::string("./" + file_name).c_str());
                goby::glog.is(goby::util::logger::WARN) &&
                    goby::glog
                        << "logging to current directory because given directory is unwritable!"
                        << std::endl;
            }
            // if still no go, quit
            if (!fout_.is_open())
            {
                goby::glog.is(goby::util::logger::DIE) &&
                    goby::glog << "cannot write to current directory, so cannot log." << std::endl;
            }

            goby::glog.add_stream(common_cfg_.log_verbosity(), &fout_);
        }
    }

    goby::moos::set_moos_technique(common_cfg_);

    if (common_cfg_.time_warp_multiplier() != 1)
    {
        goby::time::SimulatorSettings::warp_factor = common_cfg_.time_warp_multiplier();
        goby::time::SimulatorSettings::using_sim_time = true;
        goby::time::SimulatorSettings::reference_time =
            std::chrono::system_clock::time_point(std::chrono::seconds(0));
        start_time_ *= common_cfg_.time_warp_multiplier();
    }
}

// designed to run CMOOSApp derived applications
// using the MOOS "convention" of argv[1] == mission file, argv[2] == alternative name
template <typename App> int goby::moos::run(int argc, char* argv[])
{
    App::argc_ = argc;
    App::argv_ = argv;

    try
    {
        App* app = App::get_instance();
        app->Run(App::application_name_.c_str(), App::mission_file_.c_str());
    }
    catch (goby::middleware::ConfigException& e)
    {
        // no further warning as the ApplicationBase Ctor handles this
        return 1;
    }
    catch (std::exception& e)
    {
        // some other exception
        goby::glog.is(goby::util::logger::DIE) && goby::glog << "uncaught exception: " << e.what()
                                                             << std::endl;
        return 2;
    }

    return 0;
}

#endif
