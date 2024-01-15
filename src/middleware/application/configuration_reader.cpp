// Copyright 2012-2021:
//   GobySoft, LLC (2013-)
//   Massachusetts Institute of Technology (2007-2014)
//   Community contributors (see AUTHORS file)
// File authors:
//   Toby Schneider <toby@gobysoft.org>
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

#include <algorithm> // for max
#include <array>     // for array, array...
#include <cstdint>   // for int_least32_t
#include <cstdio>    // for pclose, feof
#include <cstdlib>   // for exit, EXIT_S...
#include <iostream>  // for cout, cerr
#include <iterator>  // for istreambuf_i...
#include <utility>   // for pair

#include <boost/algorithm/string/replace.hpp>           // for replace_all
#include <boost/filesystem.hpp>                         // for path, BOOST_...
#include <boost/program_options/parsers.hpp>            // for basic_comman...
#include <boost/program_options/positional_options.hpp> // for positional_o...
#include <google/protobuf/descriptor.pb.h>              // for MessageTypeT...
#include <google/protobuf/dynamic_message.h>            // for DynamicMessa...
#include <google/protobuf/message.h>                    // for Reflection
#include <google/protobuf/text_format.h>                // for TextFormat::...

#include "configuration_reader.h"
#include "goby/util/debug_logger/flex_ostream.h"    // for FlexOstream
#include "goby/util/debug_logger/flex_ostreambuf.h" // for DIE
#include "goby/util/debug_logger/term_color.h"      // for esc_nocolor
#include "goby/util/protobuf/debug_logger.pb.h"     // for GLogConfig
#include "goby/version.h"                           // for version_message

// brings std::ostream& red, etc. into scope
using namespace goby::util::tcolor;

int goby::middleware::ConfigReader::read_cfg(
    int argc, char* argv[], google::protobuf::Message* message, std::string* application_name,
    std::string* binary_name, boost::program_options::options_description* od_all,
    boost::program_options::variables_map* var_map, bool check_required_configuration /*= true*/)
{
    if (!argv)
        return 0;

    bool tool_mode = false;
    goby::GobyMessageOptions::ConfigurationOptions tool_cfg;
    if (message && message->GetDescriptor()->options().GetExtension(goby::msg).has_cfg())
    {
        tool_cfg = message->GetDescriptor()->options().GetExtension(goby::msg).cfg();
        tool_mode = tool_cfg.tool_mode();
    }

    boost::filesystem::path launch_path(argv[0]);

#if BOOST_FILESYSTEM_VERSION == 3
    *binary_name = launch_path.filename().string();
#else
    *binary_name = launch_path.filename();
#endif
    *application_name = *binary_name;

    std::string cfg_path, exec_cfg_path;

    std::string cfg_path_desc = "path to " + *application_name + " configuration file (typically " +
                                *application_name + ".pb.cfg).";

    std::string app_name_desc =
        "name to use while communicating in goby (default: " + std::string(argv[0]) + ")";

    std::map<goby::GobyFieldOptions::ConfigurationOptions::ConfigAction,
             boost::program_options::options_description>
        od_map;

    std::string od_pb_always_desc =
        "Standard options (use -hh, -hhh, or -hhh to show more options)";
    std::string od_pb_never_desc = "Hidden options";
    std::string od_pb_advanced_desc = "Advanced options";
    std::string od_pb_developer_desc = "Developer options";
    od_map.insert(
        std::make_pair(goby::GobyFieldOptions::ConfigurationOptions::ALWAYS,
                       boost::program_options::options_description(od_pb_always_desc.c_str())));
    od_map.insert(
        std::make_pair(goby::GobyFieldOptions::ConfigurationOptions::ADVANCED,
                       boost::program_options::options_description(od_pb_advanced_desc.c_str())));
    od_map.insert(
        std::make_pair(goby::GobyFieldOptions::ConfigurationOptions::DEVELOPER,
                       boost::program_options::options_description(od_pb_developer_desc.c_str())));
    od_map.insert(
        std::make_pair(goby::GobyFieldOptions::ConfigurationOptions::HIDDEN,
                       boost::program_options::options_description(od_pb_never_desc.c_str())));

    od_map.at(goby::GobyFieldOptions::ConfigurationOptions::ALWAYS)
        .add_options()("cfg_path,c", boost::program_options::value<std::string>(&cfg_path),
                       cfg_path_desc.c_str())(
            "example_config,e",
            boost::program_options::value<std::string>()->implicit_value("")->multitoken(),
            "writes an example .pb.cfg file. Use -ee to also show advanced options, -eee for "
            "developer "
            "options, and -eeee for all options");

    goby::GobyFieldOptions::ConfigurationOptions::ConfigAction shortcut_base_action_level =
        goby::GobyFieldOptions::ConfigurationOptions::ALWAYS;
    if (tool_mode)
        shortcut_base_action_level = goby::GobyFieldOptions::ConfigurationOptions::ADVANCED;

    od_map.at(shortcut_base_action_level)
        .add_options()("app_name,a", boost::program_options::value<std::string>(),
                       app_name_desc.c_str())(
            "verbose,v",
            boost::program_options::value<std::string>()->implicit_value("")->multitoken(),
            "output useful information to std::cout. -v is tty_verbosity: VERBOSE, -vv is "
            "tty_verbosity: DEBUG1, -vvv is tty_verbosity: DEBUG2, -vvvv is tty_verbosity: DEBUG3")(
            "version,V", "writes the current version");

    od_map.at(goby::GobyFieldOptions::ConfigurationOptions::ADVANCED)
        .add_options()(
            "exec_cfg_path,C", boost::program_options::value<std::string>(&exec_cfg_path),
            "File (script) to execute to create the configuration for this app. Output of "
            "application "
            "must be a TextFormat Protobuf message for this application's configuration.")(
            "glog_file_verbose,z",
            boost::program_options::value<std::string>()->implicit_value("")->multitoken(),
            "output useful information to a file (either in current directory or directory given "
            "by "
            "-d). -z is verbosity: VERBOSE, -zz is verbosity: DEBUG1, -vvv is verbosity: "
            "DEBUG2, -zzzz is verbosity: DEBUG3")("glog_file_dir,d",
                                                  boost::program_options::value<std::string>(),
                                                  "Directory for debug log (defaults to \".\"")(
            "ncurses,n", "output useful information to an NCurses GUI instead of stdout.");

    od_map.at(goby::GobyFieldOptions::ConfigurationOptions::HIDDEN)
        .add_options()("binary", boost::program_options::value<std::string>(),
                       "override binary name for help display")(
            "help,h",
            boost::program_options::value<std::string>()->implicit_value("")->multitoken(),
            "writes this help message. Use -hh for advanced options, -hhh for developer options "
            "and "
            "-hhhh for all options");

    std::map<int, std::pair<bool, std::string>> positional_options;
    if (message)
    {
        get_protobuf_program_options(od_map, message->GetDescriptor());
        get_positional_options(message->GetDescriptor(), positional_options);
        for (const auto& od_p : od_map) od_all->add(od_p.second);

        if (tool_mode)
        {
            // search for first non '-' or '--' parameter
            for (int i = 1; i < argc; ++i)
            {
                if (argv[i][0] != '-')
                {
                    // only let program options parse up to this point
                    argc = i + 1;
                    break;
                }
            }
        }
    }

    boost::program_options::positional_options_description p;

    // if any positional options are specified, don't use the defaults
    if (positional_options.empty())
    {
        positional_options.insert(std::make_pair(1, std::make_pair(false, "cfg_path")));
        positional_options.insert(std::make_pair(2, std::make_pair(false, "app_name")));
    }

    for (const auto& po_pair : positional_options)
        p.add(po_pair.second.second.c_str(), po_pair.first);

    try
    {
        boost::program_options::store(boost::program_options::command_line_parser(argc, argv)
                                          .options(*od_all)
                                          .positional(p)
                                          .run(),
                                      *var_map);
    }
    catch (std::exception& e)
    {
        throw(ConfigException(e.what()));
    }

    if (var_map->count("binary"))
    {
        *binary_name = (*var_map)["binary"].as<std::string>();
    }

    if (var_map->count("help") && tool_cfg.show_auto_help())
    {
        if (tool_cfg.show_auto_help_usage())
            write_usage(*binary_name, positional_options, &std::cerr);

        switch ((*var_map)["help"].as<std::string>().size())
        {
            case 3:
                std::cerr << od_map[goby::GobyFieldOptions::ConfigurationOptions::HIDDEN] << "\n";
                // all flow throughs intentional
            case 2:
                std::cerr << od_map[goby::GobyFieldOptions::ConfigurationOptions::DEVELOPER]
                          << "\n";
            case 1:
                std::cerr << od_map[goby::GobyFieldOptions::ConfigurationOptions::ADVANCED] << "\n";
            default:
            case 0:
                std::cerr << od_map[goby::GobyFieldOptions::ConfigurationOptions::ALWAYS] << "\n";
        }

        if (tool_cfg.exit_after_auto_help())
            exit(EXIT_SUCCESS);
    }
    else if (var_map->count("example_config"))
    {
        if (message)
        {
            switch ((*var_map)["example_config"].as<std::string>().size())
            {
                default:
                case 0:
                    get_example_cfg_file(message, &std::cout, "",
                                         goby::GobyFieldOptions::ConfigurationOptions::ALWAYS);
                    break;
                case 1:
                    get_example_cfg_file(message, &std::cout, "",
                                         goby::GobyFieldOptions::ConfigurationOptions::ADVANCED);
                    break;
                case 2:
                    get_example_cfg_file(message, &std::cout, "",
                                         goby::GobyFieldOptions::ConfigurationOptions::DEVELOPER);
                    break;
                case 3:
                    get_example_cfg_file(message, &std::cout, "",
                                         goby::GobyFieldOptions::ConfigurationOptions::HIDDEN);
                    break;
            }

            exit(EXIT_SUCCESS);
        }
        else
        {
            std::cerr << "No configuration message was provided for this application" << std::endl;
            exit(EXIT_FAILURE);
        }
    }
    else if (var_map->count("version"))
    {
        std::cout << goby::version_message() << std::endl;
        exit(EXIT_SUCCESS);
    }

    if (var_map->count("app_name"))
    {
        *application_name = (*var_map)["app_name"].as<std::string>();
    }

    boost::program_options::notify(*var_map);

    if (message)
    {
        if (cfg_path == "-")
            cfg_path = "/dev/stdin";

        if (!cfg_path.empty())
        {
            // try to read file
            std::ifstream fin(cfg_path.c_str(), std::ifstream::in);
            if (!fin.is_open())
                throw(ConfigException(std::string("could not open '" + cfg_path +
                                                  "' for reading. check value of --cfg_path")));

            std::string protobuf_text((std::istreambuf_iterator<char>(fin)),
                                      std::istreambuf_iterator<char>());
            google::protobuf::TextFormat::Parser parser;

            glog.set_name(*application_name);
            glog.add_stream(util::protobuf::GLogConfig::VERBOSE, &std::cout);
            goby::util::FlexOStreamErrorCollector error_collector(protobuf_text);
            parser.RecordErrorsTo(&error_collector);
            // maybe the command line will fill in the missing pieces
            parser.AllowPartialMessage(true);
            parser.ParseFromString(protobuf_text, message);

            if (error_collector.has_errors())
            {
                glog.is(goby::util::logger::DIE) && glog << "fatal configuration errors (see above)"
                                                         << std::endl;
            }
        }
        else if (!exec_cfg_path.empty())
        {
            FILE* pipe = popen(exec_cfg_path.c_str(), "r");
            if (!pipe)
            {
                throw(ConfigException(
                    std::string("could not execute '" + exec_cfg_path +
                                "' for retrieving the configuration. check --exec_cfg_path and "
                                "make sure it is executable.")));
            }
            else
            {
                std::string value;
                std::array<char, 256> buffer;
                while (auto bytes_read = fread(buffer.data(), sizeof(char), buffer.size(), pipe))
                    value.append(buffer.begin(), buffer.begin() + bytes_read);

                if (!feof(pipe))
                {
                    pclose(pipe);
                    throw(ConfigException(std::string("error reading output while executing '" +
                                                      exec_cfg_path + "'")));
                }

                pclose(pipe);

                if (value.empty())
                    throw(ConfigException("No data passed from -C script"));

                google::protobuf::TextFormat::Parser parser;
                parser.AllowPartialMessage(true);
                parser.ParseFromString(value, message);
            }
        }

        // add / overwrite any options that are specified in the cfg file with those given on the command line
        for (const auto& p : *var_map)
        {
            // let protobuf deal with the defaults
            if (!p.second.defaulted())
                set_protobuf_program_option(*var_map, *message, p.first, p.second);
        }

        // now the proto message must have all required fields
        if (check_required_configuration)
            check_required_cfg(*message, *binary_name);
    }

    return argc;
}

void goby::middleware::ConfigReader::set_protobuf_program_option(
    const boost::program_options::variables_map& /*var_map*/, google::protobuf::Message& message,
    const std::string& full_name, const boost::program_options::variable_value& value)
{
    const google::protobuf::Descriptor* desc = message.GetDescriptor();
    const google::protobuf::Reflection* refl = message.GetReflection();

    const google::protobuf::FieldDescriptor* field_desc = desc->FindFieldByName(full_name);
    if (!field_desc)
        return;

    if (field_desc->is_repeated())
    {
        switch (field_desc->cpp_type())
        {
            case google::protobuf::FieldDescriptor::CPPTYPE_MESSAGE:
                for (const std::string& v : value.as<std::vector<std::string>>())
                {
                    google::protobuf::TextFormat::Parser parser;
                    parser.AllowPartialMessage(true);
                    parser.MergeFromString(v, refl->AddMessage(&message, field_desc));
                }

                break;

            case google::protobuf::FieldDescriptor::CPPTYPE_INT32:
                for (google::protobuf::int32 v : value.as<std::vector<google::protobuf::int32>>())
                    refl->AddInt32(&message, field_desc, v);
                break;

            case google::protobuf::FieldDescriptor::CPPTYPE_INT64:
                for (google::protobuf::int64 v : value.as<std::vector<google::protobuf::int64>>())
                    refl->AddInt64(&message, field_desc, v);
                break;

            case google::protobuf::FieldDescriptor::CPPTYPE_UINT32:
                for (google::protobuf::uint32 v : value.as<std::vector<google::protobuf::uint32>>())
                    refl->AddUInt32(&message, field_desc, v);
                break;

            case google::protobuf::FieldDescriptor::CPPTYPE_UINT64:
                for (google::protobuf::uint64 v : value.as<std::vector<google::protobuf::uint64>>())
                    refl->AddUInt64(&message, field_desc, v);
                break;

            case google::protobuf::FieldDescriptor::CPPTYPE_BOOL:
                for (bool v : value.as<std::vector<bool>>()) refl->AddBool(&message, field_desc, v);
                break;

            case google::protobuf::FieldDescriptor::CPPTYPE_STRING:
                for (const std::string& v : value.as<std::vector<std::string>>())
                    refl->AddString(&message, field_desc, v);
                break;

            case google::protobuf::FieldDescriptor::CPPTYPE_FLOAT:
                for (float v : value.as<std::vector<float>>())
                    refl->AddFloat(&message, field_desc, v);
                break;

            case google::protobuf::FieldDescriptor::CPPTYPE_DOUBLE:
                for (double v : value.as<std::vector<double>>())
                    refl->AddDouble(&message, field_desc, v);
                break;

            case google::protobuf::FieldDescriptor::CPPTYPE_ENUM:
                for (const std::string& v : value.as<std::vector<std::string>>())
                {
                    const google::protobuf::EnumValueDescriptor* enum_desc =
                        field_desc->enum_type()->FindValueByName(v);
                    if (!enum_desc)
                        throw(ConfigException(
                            std::string("invalid enumeration " + v + " for field " + full_name)));

                    refl->AddEnum(&message, field_desc, enum_desc);
                }

                break;
        }
    }
    else
    {
        switch (field_desc->cpp_type())
        {
            case google::protobuf::FieldDescriptor::CPPTYPE_MESSAGE:
            {
                google::protobuf::TextFormat::Parser parser;
                parser.AllowPartialMessage(true);
                parser.MergeFromString(value.as<std::string>(),
                                       refl->MutableMessage(&message, field_desc));
                break;
            }

            case google::protobuf::FieldDescriptor::CPPTYPE_INT32:
                refl->SetInt32(&message, field_desc, value.as<boost::int_least32_t>());
                break;

            case google::protobuf::FieldDescriptor::CPPTYPE_INT64:
                refl->SetInt64(&message, field_desc, value.as<boost::int_least64_t>());
                break;

            case google::protobuf::FieldDescriptor::CPPTYPE_UINT32:
                refl->SetUInt32(&message, field_desc, value.as<boost::uint_least32_t>());
                break;

            case google::protobuf::FieldDescriptor::CPPTYPE_UINT64:
                refl->SetUInt64(&message, field_desc, value.as<boost::uint_least64_t>());
                break;

            case google::protobuf::FieldDescriptor::CPPTYPE_BOOL:
                refl->SetBool(&message, field_desc, value.as<bool>());
                break;

            case google::protobuf::FieldDescriptor::CPPTYPE_STRING:
                refl->SetString(&message, field_desc, value.as<std::string>());
                break;

            case google::protobuf::FieldDescriptor::CPPTYPE_FLOAT:
                refl->SetFloat(&message, field_desc, value.as<float>());
                break;

            case google::protobuf::FieldDescriptor::CPPTYPE_DOUBLE:
                refl->SetDouble(&message, field_desc, value.as<double>());
                break;

            case google::protobuf::FieldDescriptor::CPPTYPE_ENUM:
                const google::protobuf::EnumValueDescriptor* enum_desc =
                    field_desc->enum_type()->FindValueByName(value.as<std::string>());
                if (!enum_desc)
                    throw(ConfigException(std::string("invalid enumeration " +
                                                      value.as<std::string>() + " for field " +
                                                      full_name)));

                refl->SetEnum(&message, field_desc, enum_desc);
                break;
        }
    }
}

void goby::middleware::ConfigReader::get_example_cfg_file(
    google::protobuf::Message* message, std::ostream* stream, const std::string& indent,
    goby::GobyFieldOptions::ConfigurationOptions::ConfigAction action)
{
    build_description(message->GetDescriptor(), *stream, indent, false, action);
    *stream << std::endl;
}

void goby::middleware::ConfigReader::get_positional_options(
    const google::protobuf::Descriptor* desc,
    std::map<int, std::pair<bool, std::string>>& positional_options)
{
    for (int i = 0, n = desc->field_count(); i < n; ++i)
    {
        const google::protobuf::FieldDescriptor* field_desc = desc->field(i);
        const std::string& field_name = field_desc->name();
        const std::string& cli_name = field_name;

        const goby::GobyFieldOptions::ConfigurationOptions& cfg_opts =
            field_desc->options().GetExtension(goby::field).cfg();

        if (cfg_opts.has_position())
        {
            if (positional_options.count(cfg_opts.position()))
                throw(ConfigException(
                    "(goby.field).cfg.position = " + std::to_string(cfg_opts.position()) +
                    " is specified multiple times in the config proto file: \"" +
                    positional_options.at(cfg_opts.position()).second + "\" and \"" + field_name +
                    "\""));

            positional_options.insert(std::make_pair(
                cfg_opts.position(), std::make_pair(field_desc->is_required(), cli_name)));
        }
    }
}

void goby::middleware::ConfigReader::get_protobuf_program_options(
    std::map<goby::GobyFieldOptions::ConfigurationOptions::ConfigAction,
             boost::program_options::options_description>& od_map,
    const google::protobuf::Descriptor* desc)
{
    for (int i = 0, n = desc->field_count(); i < n; ++i)
    {
        const google::protobuf::FieldDescriptor* field_desc = desc->field(i);
        const std::string& field_name = field_desc->name();
        std::string cli_name = field_name;

        const goby::GobyFieldOptions::ConfigurationOptions& cfg_opts =
            field_desc->options().GetExtension(goby::field).cfg();

        if (cfg_opts.has_cli_short())
            cli_name += "," + cfg_opts.cli_short();

        boost::program_options::options_description& po_desc = od_map.at(cfg_opts.action());

        std::stringstream human_desc_ss;
        human_desc_ss << util::esc_lt_blue
                      << field_desc->options().GetExtension(goby::field).description();

        if (field_desc->cpp_type() == google::protobuf::FieldDescriptor::CPPTYPE_ENUM)
        {
            human_desc_ss << " (";
            for (int i = 0, n = field_desc->enum_type()->value_count(); i < n; ++i)
            {
                if (i)
                    human_desc_ss << ", ";
                human_desc_ss << field_desc->enum_type()->value(i)->name();
            }

            human_desc_ss << ")";
        }

        human_desc_ss << label(field_desc);
        human_desc_ss << " " << util::esc_nocolor;

        switch (field_desc->cpp_type())
        {
            case google::protobuf::FieldDescriptor::CPPTYPE_MESSAGE:
            {
                build_description(field_desc->message_type(), human_desc_ss, "");

                set_single_option(po_desc, field_desc, std::string(), cli_name,
                                  human_desc_ss.str());
            }
            break;

            case google::protobuf::FieldDescriptor::CPPTYPE_INT32:
            {
                set_single_option(po_desc, field_desc, field_desc->default_value_int32(), cli_name,
                                  human_desc_ss.str());
            }
            break;

            case google::protobuf::FieldDescriptor::CPPTYPE_INT64:
            {
                set_single_option(po_desc, field_desc, field_desc->default_value_int64(), cli_name,
                                  human_desc_ss.str());
            }
            break;

            case google::protobuf::FieldDescriptor::CPPTYPE_UINT32:
            {
                set_single_option(po_desc, field_desc, field_desc->default_value_uint32(), cli_name,
                                  human_desc_ss.str());
            }
            break;

            case google::protobuf::FieldDescriptor::CPPTYPE_UINT64:
            {
                set_single_option(po_desc, field_desc, field_desc->default_value_uint64(), cli_name,
                                  human_desc_ss.str());
            }
            break;

            case google::protobuf::FieldDescriptor::CPPTYPE_BOOL:
            {
                set_single_option(po_desc, field_desc, field_desc->default_value_bool(), cli_name,
                                  human_desc_ss.str());
            }
            break;

            case google::protobuf::FieldDescriptor::CPPTYPE_STRING:
            {
                set_single_option(po_desc, field_desc, field_desc->default_value_string(), cli_name,
                                  human_desc_ss.str());
            }
            break;

            case google::protobuf::FieldDescriptor::CPPTYPE_FLOAT:
            {
                set_single_option(po_desc, field_desc, field_desc->default_value_float(), cli_name,
                                  human_desc_ss.str());
            }
            break;

            case google::protobuf::FieldDescriptor::CPPTYPE_DOUBLE:
            {
                set_single_option(po_desc, field_desc, field_desc->default_value_double(), cli_name,
                                  human_desc_ss.str());
            }
            break;

            case google::protobuf::FieldDescriptor::CPPTYPE_ENUM:
            {
                set_single_option(po_desc, field_desc, field_desc->default_value_enum()->name(),
                                  cli_name, human_desc_ss.str());
            }
            break;
        }
    }
}

void goby::middleware::ConfigReader::build_description(
    const google::protobuf::Descriptor* desc, std::ostream& stream,
    const std::string& indent /*= ""*/, bool use_color /* = true */,
    goby::GobyFieldOptions::ConfigurationOptions::ConfigAction action /*=
                                                                        goby::GobyFieldOptions::ConfigurationOptions::ALWAYS*/ )
{
    for (int i = 0, n = desc->field_count(); i < n; ++i)
    {
        const google::protobuf::FieldDescriptor* field_desc = desc->field(i);

        if (field_desc->options().GetExtension(goby::field).cfg().action() > action)
            continue;

        build_description_field(field_desc, stream, indent, use_color, action);
    }

    std::vector<const google::protobuf::FieldDescriptor*> extensions;
    google::protobuf::DescriptorPool::generated_pool()->FindAllExtensions(desc, &extensions);
    for (auto field_desc : extensions)
    {
        if (field_desc->options().GetExtension(goby::field).cfg().action() > action)
            continue;

        build_description_field(field_desc, stream, indent, use_color, action);
    }
}

void goby::middleware::ConfigReader::build_description_field(
    const google::protobuf::FieldDescriptor* field_desc, std::ostream& stream,
    const std::string& indent, bool use_color,
    goby::GobyFieldOptions::ConfigurationOptions::ConfigAction action)
{
    google::protobuf::DynamicMessageFactory factory;
    const google::protobuf::Message* default_msg =
        factory.GetPrototype(field_desc->containing_type());

    if (field_desc->cpp_type() == google::protobuf::FieldDescriptor::CPPTYPE_MESSAGE)
    {
        std::string before_description = indent;

        if (field_desc->is_extension())
        {
            if (field_desc->extension_scope())
                before_description +=
                    "[" + field_desc->extension_scope()->full_name() + "." + field_desc->name();
            else
                before_description += "[" + field_desc->full_name();
        }
        else
        {
            before_description += field_desc->name();
        }

        if (field_desc->is_extension())
            before_description += "]";

        before_description += " {  ";
        stream << "\n" << before_description;

        std::string description;
        if (use_color)
            description += util::esc_green;
        else
            description += "# ";

        description +=
            field_desc->options().GetExtension(goby::field).description() + label(field_desc);

        if (use_color)
            description += " " + util::esc_nocolor;

        if (!use_color)
            wrap_description(&description, before_description.size());

        stream << description;

        build_description(field_desc->message_type(), stream, indent + "  ", use_color, action);
        stream << "\n" << indent << "}";
    }
    else
    {
        stream << "\n";

        std::string before_description = indent;

        std::string example;
        if (field_desc->has_default_value())
            google::protobuf::TextFormat::PrintFieldValueToString(*default_msg, field_desc, -1,
                                                                  &example);
        else
        {
            example = field_desc->options().GetExtension(goby::field).example();
            if (field_desc->cpp_type() == google::protobuf::FieldDescriptor::CPPTYPE_STRING)
                example = "\"" + example + "\"";
        }

        if (field_desc->is_extension())
        {
            if (field_desc->extension_scope())
                before_description +=
                    "[" + field_desc->extension_scope()->full_name() + "." + field_desc->name();
            else
                before_description += "[" + field_desc->full_name();
        }
        else
        {
            before_description += field_desc->name();
        }

        if (field_desc->is_extension())
            before_description += "]";

        before_description += ": ";
        before_description += example;
        before_description += "  ";

        stream << before_description;

        std::string description;

        if (use_color)
            description += util::esc_green;
        else
            description += "# ";

        description += field_desc->options().GetExtension(goby::field).description();
        if (field_desc->cpp_type() == google::protobuf::FieldDescriptor::CPPTYPE_ENUM)
        {
            description += " (";
            for (int i = 0, n = field_desc->enum_type()->value_count(); i < n; ++i)
            {
                if (i)
                    description += ", ";
                description += field_desc->enum_type()->value(i)->name();
            }

            description += ")";
        }

        description += label(field_desc);

        if (field_desc->has_default_value())
            description += " (default=" + example + ")";

        if (field_desc->options().GetExtension(goby::field).has_moos_global())
            description += " (can also set MOOS global \"" +
                           field_desc->options().GetExtension(goby::field).moos_global() + "=\")";

        if (!use_color)
            wrap_description(&description, before_description.size());

        stream << description;

        if (use_color)
            stream << " " << util::esc_nocolor;
    }
}

std::string
goby::middleware::ConfigReader::label(const google::protobuf::FieldDescriptor* field_desc)
{
    switch (field_desc->label())
    {
        case google::protobuf::FieldDescriptor::LABEL_REQUIRED: return " (required)";

        case google::protobuf::FieldDescriptor::LABEL_OPTIONAL: return " (optional)";

        case google::protobuf::FieldDescriptor::LABEL_REPEATED: return " (repeated)";
    }

    return "";
}

std::string goby::middleware::ConfigReader::word_wrap(std::string s, unsigned width,
                                                      const std::string& delim)
{
    std::string out;

    while (s.length() > width)
    {
        std::string::size_type pos_newline = s.find('\n');
        std::string::size_type pos_delim = s.substr(0, width).find_last_of(delim);
        if (pos_newline < width)
        {
            out += s.substr(0, pos_newline);
            s = s.substr(pos_newline + 1);
        }
        else if (pos_delim != std::string::npos)
        {
            out += s.substr(0, pos_delim + 1);
            s = s.substr(pos_delim + 1);
        }
        else
        {
            out += s.substr(0, width);
            s = s.substr(width);
        }
        out += "\n";

        // std::cout << "width: " << width << " " << out << std::endl;
    }
    out += s;

    return out;
}

void goby::middleware::ConfigReader::wrap_description(std::string* description, int num_blanks)
{
    *description =
        word_wrap(*description, std::max(MAX_CHAR_PER_LINE - num_blanks, (int)MIN_CHAR), " ");

    if (MIN_CHAR > MAX_CHAR_PER_LINE - num_blanks)
    {
        *description = "\n" + description->substr(2);
        num_blanks = MAX_CHAR_PER_LINE - MIN_CHAR;
    }

    std::string spaces(num_blanks, ' ');
    spaces += "# ";
    boost::replace_all(*description, "\n", "\n" + spaces);
}

void goby::middleware::ConfigReader::check_required_cfg(const google::protobuf::Message& message,
                                                        const std::string& binary)
{
    if (!message.IsInitialized())
    {
        std::vector<std::string> errors;
        message.FindInitializationErrors(&errors);

        std::stringstream err_msg;
        err_msg << "Configuration is missing required parameters: \n";
        for (const std::string& s : errors)
            err_msg << util::esc_red << s << "\n" << util::esc_nocolor;

        std::map<int, std::pair<bool, std::string>> positional_options;
        get_positional_options(message.GetDescriptor(), positional_options);

        write_usage(binary, positional_options, &err_msg);

        //        err_msg << "Make sure you specified a proper `cfg_path` to the configuration file.";
        throw(ConfigException(err_msg.str()));
    }
}
