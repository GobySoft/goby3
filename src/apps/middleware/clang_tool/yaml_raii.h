// Copyright 2020:
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

#ifndef GOBY_APPS_MIDDLEWARE_CLANG_TOOL_YAML_RAII_H
#define GOBY_APPS_MIDDLEWARE_CLANG_TOOL_YAML_RAII_H

#include "yaml-cpp/yaml.h"

namespace goby
{
namespace yaml
{
class YSeq
{
  public:
    YSeq(YAML::Emitter& out, bool flow = false) : out_(out)
    {
        if (flow)
            out << YAML::Flow;
        else
            out << YAML::Block;

        out << YAML::BeginSeq;
    }
    ~YSeq() { out_ << YAML::EndSeq; }

    template <typename A> void add(A a) { out_ << a; }

  private:
    YAML::Emitter& out_;
};

class YMap
{
  public:
    YMap(YAML::Emitter& out, bool flow = false) : out_(out)
    {
        if (flow)
            out << YAML::Flow;
        else
            out << YAML::Block;

        out << YAML::BeginMap;
    }

    template <typename A, typename B> YMap(YAML::Emitter& out, A key, B value) : YMap(out)
    {
        add(key, value);
    }

    template <typename A> YMap(YAML::Emitter& out, A key) : YMap(out) { add_key(key); }

    ~YMap() { out_ << YAML::EndMap; }

    template <typename A, typename B> void add(A key, B value)
    {
        out_ << YAML::Key << key << YAML::Value << value;
    }

    template <typename A> void add(A key, std::string value)
    {
        bool double_quote = value.find(' ') != std::string::npos;
        if (double_quote)
            out_ << YAML::Key << key << YAML::Value << YAML::DoubleQuoted << value;
        else
            add<A, std::string>(key, value);
    }

    template <typename A> void add_key(A key) { out_ << YAML::Key << key << YAML::Value; }

  private:
    YAML::Emitter& out_;
};
} // namespace yaml
} // namespace goby

#endif
