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

#ifndef CONFIGURATIONREADER20100929H
#define CONFIGURATIONREADER20100929H

#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>

#include <boost/program_options.hpp>

#include <google/protobuf/descriptor.h>
#include <google/protobuf/descriptor.pb.h>
#include <google/protobuf/io/zero_copy_stream.h>
#include <google/protobuf/io/zero_copy_stream_impl.h>
#include <google/protobuf/message.h>
#include <google/protobuf/text_format.h>

#include "goby/exception.h"
#include "goby/util/as.h"

class AppBaseConfig;

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

class ConfigReader
{
  public:
    static void read_cfg(int argc, char* argv[], google::protobuf::Message* message,
                         std::string* application_name,
                         boost::program_options::options_description* od_all,
                         boost::program_options::variables_map* var_map,
                         bool check_required_configuration = true);

    static void check_required_cfg(const google::protobuf::Message& message);

    static void get_protobuf_program_options(boost::program_options::options_description& po_desc,
                                             const google::protobuf::Descriptor* desc);

    static void set_protobuf_program_option(const boost::program_options::variables_map& vm,
                                            google::protobuf::Message& message,
                                            const std::string& full_name,
                                            const boost::program_options::variable_value& value);

    static void get_example_cfg_file(google::protobuf::Message* message,
                                     std::ostream* human_desc_ss, const std::string& indent = "");

  private:
    enum
    {
        MAX_CHAR_PER_LINE = 66
    };
    enum
    {
        MIN_CHAR = 20
    };

    static void build_description(const google::protobuf::Descriptor* desc,
                                  std::ostream& human_desc, const std::string& indent = "",
                                  bool use_color = true);

    static void build_description_field(const google::protobuf::FieldDescriptor* desc,
                                        std::ostream& human_desc, const std::string& indent,
                                        bool use_color);

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
                    boost::program_options::value<std::vector<T> >()->default_value(
                        std::vector<T>(1, default_value),
                        goby::util::as<std::string>(default_value)),
                    description.c_str());
            }
            else
            {
                po_desc.add_options()(name.c_str(),
                                      boost::program_options::value<std::vector<T> >(),
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
};
} // namespace common
} // namespace goby

#endif
