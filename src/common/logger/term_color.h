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

#ifndef TermColor20091211H
#define TermColor20091211H

#include <iostream>
#include <map>
#include <memory>
#include <string>

namespace goby
{
namespace common
{
const std::string esc_red = "\33[31m";
const std::string esc_lt_red = "\33[91m";
const std::string esc_green = "\33[32m";
const std::string esc_lt_green = "\33[92m";
const std::string esc_yellow = "\33[33m";
const std::string esc_lt_yellow = "\33[93m";
const std::string esc_blue = "\33[34m";
const std::string esc_lt_blue = "\33[94m";
const std::string esc_magenta = "\33[35m";
const std::string esc_lt_magenta = "\33[95m";
const std::string esc_cyan = "\33[36m";
const std::string esc_lt_cyan = "\33[96m";
const std::string esc_white = "\33[37m";
const std::string esc_lt_white = "\33[97m";
const std::string esc_nocolor = "\33[0m";

/// Contains functions for adding color to Terminal window streams
namespace tcolor
{
/// Append the given escape code to the stream os
/// \param os ostream to append to
/// \param esc_code escape code to append (e.g. "\33[31m")
std::ostream& add_escape_code(std::ostream& os, const std::string& esc_code);

/// All text following this manipulator is red. (e.g. std::cout << red << "text";)
inline std::ostream& red(std::ostream& os) { return (add_escape_code(os, esc_red)); }

/// All text following this manipulator is light red (e.g. std::cout << lt_red << "text";)
inline std::ostream& lt_red(std::ostream& os) { return (add_escape_code(os, esc_lt_red)); }

/// All text following this manipulator is green (e.g. std::cout << green << "text";)
inline std::ostream& green(std::ostream& os) { return (add_escape_code(os, esc_green)); }

/// All text following this manipulator is light green (e.g. std::cout << lt_green << "text";)
inline std::ostream& lt_green(std::ostream& os) { return (add_escape_code(os, esc_lt_green)); }

/// All text following this manipulator is yellow (e.g. std::cout << yellow << "text";)
inline std::ostream& yellow(std::ostream& os) { return (add_escape_code(os, esc_yellow)); }

/// All text following this manipulator is light yellow  (e.g. std::cout << lt_yellow << "text";)
inline std::ostream& lt_yellow(std::ostream& os) { return (add_escape_code(os, esc_lt_yellow)); }

/// All text following this manipulator is blue (e.g. std::cout << blue << "text";)
inline std::ostream& blue(std::ostream& os) { return (add_escape_code(os, esc_blue)); }

/// All text following this manipulator is light blue (e.g. std::cout << lt_blue << "text";)
inline std::ostream& lt_blue(std::ostream& os) { return (add_escape_code(os, esc_lt_blue)); }

/// All text following this manipulator is magenta (e.g. std::cout << magenta << "text";)
inline std::ostream& magenta(std::ostream& os) { return (add_escape_code(os, esc_magenta)); }

/// All text following this manipulator is light magenta (e.g. std::cout << lt_magenta << "text";)
inline std::ostream& lt_magenta(std::ostream& os) { return (add_escape_code(os, esc_lt_magenta)); }

/// All text following this manipulator is cyan (e.g. std::cout << cyan << "text";)
inline std::ostream& cyan(std::ostream& os) { return (add_escape_code(os, esc_cyan)); }

/// All text following this manipulator is light cyan (e.g. std::cout << lt_cyan << "text";)
inline std::ostream& lt_cyan(std::ostream& os) { return (add_escape_code(os, esc_lt_cyan)); }

/// All text following this manipulator is white (e.g. std::cout << white << "text";)
inline std::ostream& white(std::ostream& os) { return (add_escape_code(os, esc_white)); }

/// All text following this manipulator is bright white (e.g. std::cout << lt_white << "text";)
inline std::ostream& lt_white(std::ostream& os) { return (add_escape_code(os, esc_lt_white)); }

/// All text following this manipulator is uncolored (e.g. std::cout << green << "green" << nocolor << "uncolored";)
inline std::ostream& nocolor(std::ostream& os) { return (add_escape_code(os, esc_nocolor)); }
} // namespace tcolor

/// \brief Represents the eight available terminal colors (and bold variants)
struct Colors
{
    /// \brief The eight terminal colors (and bold or "light" variants)
    enum Color
    {
        nocolor,
        red,
        lt_red,
        green,
        lt_green,
        yellow,
        lt_yellow,
        blue,
        lt_blue,
        magenta,
        lt_magenta,
        cyan,
        lt_cyan,
        white,
        lt_white
    };
};

/// \brief Converts between string, escape code, and enumeration representations of the terminal colors
class TermColor
{
  public:
    /// Color enumeration from string (e.g. "blue" -> blue)
    static Colors::Color from_str(const std::string& s) { return get_instance()->priv_from_str(s); }

    /// String from color enumeration (e,g, red -> "red")
    static std::string str_from_col(const Colors::Color& c)
    {
        return get_instance()->priv_str_from_col(c);
    }

    /// Color enumeration from escape code (e,g, "\33[31m" -> red)
    static Colors::Color from_esc_code(const std::string& s)
    {
        return get_instance()->priv_from_esc_code(s);
    }

    /// Escape code from color enumeration (e.g. red -> "\33[31m")
    static std::string esc_code_from_col(const Colors::Color& c)
    {
        return get_instance()->priv_esc_code_from_col(c);
    }

    /// Escape code from string (e.g. "red" -> "\33[31m")
    static std::string esc_code_from_str(const std::string& s)
    {
        return get_instance()->priv_esc_code_from_str(s);
    }

  private:
    TermColor();
    ~TermColor() {}

    TermColor(const TermColor&);
    TermColor& operator=(const TermColor&);

    static TermColor* get_instance()
    {
        static TermColor t;
        return &t;
    }

    Colors::Color priv_from_str(const std::string& s) { return colors_map_[s]; }

    // red -> "red"
    std::string priv_str_from_col(const Colors::Color& c)
    {
        typedef std::pair<std::string, Colors::Color> P;
        for (const P& p : colors_map_)
        {
            if (p.second == c)
                return p.first;
        }
        return "nocolor";
    }

    // "\33[31m" -> red
    Colors::Color priv_from_esc_code(const std::string& s) { return esc_code_map_[s]; }

    // red -> "\33[31m"
    std::string priv_esc_code_from_col(const Colors::Color& c)
    {
        typedef std::pair<std::string, Colors::Color> P;
        for (const P& p : esc_code_map_)
        {
            if (p.second == c)
                return p.first;
        }
        return esc_nocolor;
    }

    // "red" -> "\33[31m"
    std::string priv_esc_code_from_str(const std::string& s)
    {
        return esc_code_from_col(from_str(s));
    }

  private:
    std::map<std::string, Colors::Color> colors_map_;
    std::map<std::string, Colors::Color> esc_code_map_;
};
} // namespace common
} // namespace goby

#endif
