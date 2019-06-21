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

#ifndef Group20170807H
#define Group20170807H

#include <memory>
#include <string>

#ifndef __clang__
#ifdef __GNUC__
#if __GNUC__ < 7 || (__GNUC__ == 7 && __GNUC_MINOR__ < 2)
// bug in gcc < 7.2 requires extern
// https://gcc.gnu.org/bugzilla/show_bug.cgi?id=52036
#error Must use Clang or GCC > 7.2 to compile goby3 middleware
#endif
#endif
#endif

namespace goby
{
namespace middleware
{
class Group
{
  public:
    static constexpr std::uint8_t broadcast_group{255};
    static constexpr std::uint8_t invalid_numeric_group{0};

    constexpr Group(const char* c, std::uint8_t i = invalid_numeric_group) : c_(c), i_(i) {}
    constexpr Group(std::uint8_t i = invalid_numeric_group) : i_(i) {}

    constexpr std::uint8_t numeric() const { return i_; }
    constexpr const char* c_str() const { return c_; }

    operator std::string() const
    {
        if (c_ != nullptr)
            return std::string(c_);
        else
            return std::to_string(i_);
    }

  protected:
    void set_c_str(const char* c) { c_ = c; }

  private:
    const char* c_{nullptr};
    std::uint8_t i_{invalid_numeric_group};
};

inline bool operator==(const Group& a, const Group& b)
{
    if (a.c_str() != nullptr && b.c_str() != nullptr)
        return std::string(a.c_str()) == std::string(b.c_str());
    else
        return a.numeric() == b.numeric();
}

inline bool operator!=(const Group& a, const Group& b) { return !(a == b); }

inline std::ostream& operator<<(std::ostream& os, const Group& g) { return (os << std::string(g)); }

class DynamicGroup : public Group
{
  public:
    DynamicGroup(const std::string& s, std::uint8_t i = Group::invalid_numeric_group)
        : Group(i), s_(new std::string(s))
    {
        Group::set_c_str(s_->c_str());
    }

    DynamicGroup(std::uint8_t i) : Group(i) {}

  private:
    std::unique_ptr<const std::string> s_;
};

} // namespace middleware
} // namespace goby

#endif
