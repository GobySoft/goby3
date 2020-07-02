// Copyright 2017-2020:
//   GobySoft, LLC (2013-)
//   Community contributors (see AUTHORS file)
// File authors:
//   Toby Schneider <toby@gobysoft.org>
//   Ryan Govostes <rgovostes+git@gmail.com>
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
/// Objects implementing the Goby nested middleware.
namespace middleware
{
/// \brief Class for grouping publications in the Goby middleware. Analogous to "topics" in ROS, "channel" in LCM, or "variable" in MOOS.
///
/// A Group is defined by a string and possibly also an integer value (when used on intervehicle and outer layers). For interprocess and inner layers, the string value is used (and the integer value is optional). For intervehicle and outer layers, the integer value is used to minimizing wire size over these restricted links.
///
/// Group is intended to instantiated as a compile-time constant (\c constexpr), e.g.
/// \code
/// // For use on interprocess or inner (string only)
/// constexpr goby::middleware::Group example_navigation{"navigation"};
/// // For use on all layers (string and numeric)
/// constexpr goby::middleware::Group example_status{"status", 2};
/// \endcode
class Group
{
  public:
    /// Special group number representing the broadcast group (used when no grouping is required for a given type)
    static constexpr std::uint8_t broadcast_group{0};
    /// Special group number representing an invalid numeric group (unsuitable for intervehicle and outer layers)
    static constexpr std::uint8_t invalid_numeric_group{255};

    /// \brief Construct a group with a (C-style) string and possibly a numeric value (when this Group will be used on intervehicle and outer layers).
    constexpr Group(const char* c, std::uint8_t i = invalid_numeric_group) : c_(c), i_(i) {}

    /// \brief Construct a group with only a numeric value
    constexpr Group(std::uint8_t i = invalid_numeric_group) : i_(i) {}

    /// \brief Access the group's numeric value
    constexpr std::uint8_t numeric() const { return i_; }

    /// \brief Access the group's string value as a C string
    constexpr const char* c_str() const { return c_; }

    /// \brief Access the group's string value as a C++ string
    operator std::string() const
    {
        if (c_ != nullptr)
        {
            if (i_ == invalid_numeric_group)
                return std::string(c_);
            else
                return std::string(c_) + "::" + std::to_string(i_);
        }
        else
        {
            return std::to_string(i_);
        }
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
        return (std::string(a.c_str()) == std::string(b.c_str())) && (a.numeric() == b.numeric());
    else
        return a.numeric() == b.numeric();
}

inline bool operator!=(const Group& a, const Group& b) { return !(a == b); }

inline std::ostream& operator<<(std::ostream& os, const Group& g)
{
    if (g.c_str() != nullptr)
        return (os << std::string(g) + "::" + std::to_string(g.numeric()));
    else
        return (os << g.numeric());
}

/// \brief Implementation of Group for dynamic (run-time) instantiations. Use Group directly for static (compile-time) instantiations.
class DynamicGroup : public Group
{
  public:
    /// \brief Construct a group with a string and possibly a numeric value (when this Group will be used on intervehicle and outer layers).
    DynamicGroup(const std::string& s, std::uint8_t i = Group::invalid_numeric_group)
        : Group(i), s_(new std::string(s))
    {
        Group::set_c_str(s_->c_str());
    }

    /// \brief Construct a group with a numeric value only
    DynamicGroup(std::uint8_t i) : Group(i) {}

  private:
    std::unique_ptr<const std::string> s_;
};

} // namespace middleware
} // namespace goby

namespace std
{
template <> struct hash<goby::middleware::Group>
{
    size_t operator()(const goby::middleware::Group& group) const noexcept
    {
        return std::hash<std::string>{}(std::string(group));
    }
};
} // namespace std

#endif
