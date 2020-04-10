#ifndef YAMLRAII_20190801_H
#define YAMLRAII_20190801_H

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
