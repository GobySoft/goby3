// Copyright 2012-2024:
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

#ifndef GOBY_MIDDLEWARE_APPLICATION_CONFIGURATION_READER_H
#define GOBY_MIDDLEWARE_APPLICATION_CONFIGURATION_READER_H

#include <fstream> // for ostream
#include <string>  // for string
#include <vector>  // for vector

#include <boost/lexical_cast/bad_lexical_cast.hpp>       // for bad_lexical...
#include <boost/program_options/options_description.hpp> // for options_des...
#include <boost/program_options/value_semantic.hpp>      // for value, bool...
#include <boost/program_options/variables_map.hpp>       // for variable_va...
#include <google/protobuf/descriptor.h>                  // for FieldDescri...

#include "goby/exception.h"                     // for Exception
#include "goby/protobuf/option_extensions.pb.h" // for GobyFieldOpt...
#include "goby/util/as.h"                       // for as

namespace google
{
namespace protobuf
{
class Message;
} // namespace protobuf
} // namespace google

namespace goby
{
namespace middleware
{
///  \brief  indicates a problem with the runtime command line
/// or .cfg file configuration (or --help was given)
class ConfigException : public Exception
{
  public:
    ConfigException(const std::string& s) : Exception(s) {}

  private:
};

/// \brief Class for reading configuration from command line and/or file(s) into a Google Protocol Buffers message. You will likely want to use ProtobufConfigurator rather than using this class directly.
/// \todo Rewrite to clean this up
class ConfigReader
{
  public:
    /// \brief Read the configuration into a Protobuf message using the command line parameters
    ///
    /// \param argc Argument count
    /// \param argv Command line arguments
    /// \param message Pointer to Protobuf message to populate with the configuration
    /// \param application_name Pointer to string to populate with the application name (defaults to filename of argv[0], can be overridden with --app_name)
    /// \param od_all Pointer to boost::program::options_description that will be populated with all the available command-line options
    /// \param var_map Pointer to boost::program_options::variables_map that will be populated with the variables read from the command line
    /// \param check_required_configuration If true, check_required_cfg will be called after populating the message
    /// \return maximum argc value read (if using tool_mode this may be less than argc)
    static int read_cfg(int argc, char* argv[], google::protobuf::Message* message,
                        std::string* application_name, std::string* binary_name,
                        boost::program_options::options_description* od_all,
                        boost::program_options::variables_map* var_map,
                        bool check_required_configuration = true);

    /// \brief Checks that all \c required fields are set (either via the command line or the configuration file) in the Protobuf message.
    ///
    /// \param message Message to check
    /// \throw ConfigException if any \c required fields are unset
    static void check_required_cfg(const google::protobuf::Message& message,
                                   const std::string& binary);

    static void get_protobuf_program_options(
        std::map<goby::GobyFieldOptions::ConfigurationOptions::ConfigAction,
                 boost::program_options::options_description>& od_map,
        const google::protobuf::Descriptor* desc,
        std::map<std::string, std::string>& environmental_var_map);

    struct PositionalOption
    {
        std::string name;
        bool required;
        int position_max_count; // -1 is infinity
    };

    static void get_positional_options(const google::protobuf::Descriptor* desc,
                                       // position -> required -> name
                                       std::vector<PositionalOption>& positional_options);

    static void set_protobuf_program_option(const boost::program_options::variables_map& vm,
                                            google::protobuf::Message& message,
                                            const std::string& full_name,
                                            const boost::program_options::variable_value& value,
                                            bool overwrite_if_exists);

    static void
    get_example_cfg_file(google::protobuf::Message* message, std::ostream* human_desc_ss,
                         const std::string& indent = "",
                         goby::GobyFieldOptions::ConfigurationOptions::ConfigAction action =
                             goby::GobyFieldOptions::ConfigurationOptions::ALWAYS);

  private:
    enum
    {
        MAX_CHAR_PER_LINE = 66
    };
    enum
    {
        MIN_CHAR = 20
    };

    static void
    build_description(const google::protobuf::Descriptor* desc, std::ostream& human_desc,
                      const std::string& indent = "", bool use_color = true,
                      goby::GobyFieldOptions::ConfigurationOptions::ConfigAction action =
                          goby::GobyFieldOptions::ConfigurationOptions::ALWAYS);

    static void
    build_description_field(const google::protobuf::FieldDescriptor* desc, std::ostream& human_desc,
                            const std::string& indent, bool use_color,
                            goby::GobyFieldOptions::ConfigurationOptions::ConfigAction action);

    static void wrap_description(std::string* description, int num_blanks);

    static std::string label(const google::protobuf::FieldDescriptor* field_desc);

    static std::string word_wrap(std::string s, unsigned width, const std::string& delim);

    template <typename T>
    static void set_single_option(boost::program_options::options_description& po_desc,
                                  const google::protobuf::FieldDescriptor* field_desc,
                                  const T& default_value, const std::string& name,
                                  const std::string& description)
    {
        if (!field_desc->is_repeated())
        {
            if (field_desc->has_default_value())
            {
                po_desc.add_options()(
                    name.c_str(), boost::program_options::value<T>()->default_value(default_value),
                    description.c_str());
            }
            else
            {
                po_desc.add_options()(name.c_str(), boost::program_options::value<T>(),
                                      description.c_str());
            }
        }
        else
        {
            if (field_desc->has_default_value())
            {
                po_desc.add_options()(
                    name.c_str(),
                    boost::program_options::value<std::vector<T>>()
                        ->default_value(std::vector<T>(1, default_value),
                                        goby::util::as<std::string>(default_value))
                        ->composing(),
                    description.c_str());
            }
            else
            {
                po_desc.add_options()(name.c_str(),
                                      boost::program_options::value<std::vector<T>>()->composing(),
                                      description.c_str());
            }
        }
    }

    // special case for bool presence/absence when default is false
    // for example, for the bool field "foo", "--foo" is true and omitting "--foo" is false, rather than --foo=true/false
    static void set_single_option(boost::program_options::options_description& po_desc,
                                  const google::protobuf::FieldDescriptor* field_desc,
                                  bool default_value, const std::string& name,
                                  const std::string& description)
    {
        if (!field_desc->is_repeated() && field_desc->has_default_value() && default_value == false)
        {
            po_desc.add_options()(
                name.c_str(), boost::program_options::bool_switch()->default_value(default_value),
                description.c_str());
        }
        else
        {
            set_single_option<bool>(po_desc, field_desc, default_value, name, description);
        }
    }

    static void write_usage(const std::string& binary,
                            const std::vector<PositionalOption>& positional_options,
                            std::ostream* out)
    {
        (*out) << "Usage: " << binary << " ";
        for (const auto& po : positional_options)
        {
            if (!po.required)
                (*out) << "[";
            (*out) << "<" << po.name;
            if (po.position_max_count > 1)
                (*out) << "(" << po.position_max_count << ")";
            else if (po.position_max_count == -1)
                (*out) << "(...)";
            (*out) << ">";
            if (!po.required)
                (*out) << "]";
            (*out) << " ";
        }
        (*out) << "[options]\n";
    }
};
} // namespace middleware
} // namespace goby

#endif
