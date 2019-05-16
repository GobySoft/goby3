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

#include "term_color.h"
#include "flex_ostream.h"

// TODO(tes): See if this dynamic cast is unncessary now
std::ostream& goby::common::tcolor::add_escape_code(std::ostream& os, const std::string& esc_code)
{
    try
    {
        FlexOstream& flex = dynamic_cast<FlexOstream&>(os);
        return (flex << esc_code);
    }
    catch (const std::bad_cast& e)
    {
        return (os);
    }
}

goby::common::TermColor::TermColor()
{
    boost::assign::insert(colors_map_)("nocolor", Colors::nocolor)("red", Colors::red)(
        "lt_red", Colors::lt_red)("green", Colors::green)("lt_green", Colors::lt_green)(
        "yellow", Colors::yellow)("lt_yellow", Colors::lt_yellow)("blue", Colors::blue)(
        "lt_blue", Colors::lt_blue)("magenta", Colors::magenta)("lt_magenta", Colors::lt_magenta)(
        "cyan", Colors::cyan)("lt_cyan", Colors::lt_cyan)("white", Colors::white)("lt_white",
                                                                                  Colors::lt_white);

    boost::assign::insert(esc_code_map_)(esc_nocolor, Colors::nocolor)(esc_red, Colors::red)(
        esc_lt_red, Colors::lt_red)(esc_green, Colors::green)(esc_lt_green, Colors::lt_green)(
        esc_yellow, Colors::yellow)(esc_lt_yellow, Colors::lt_yellow)(esc_blue, Colors::blue)(
        esc_lt_blue, Colors::lt_blue)(esc_magenta, Colors::magenta)(
        esc_lt_magenta, Colors::lt_magenta)(esc_cyan, Colors::cyan)(esc_lt_cyan, Colors::lt_cyan)(
        esc_white, Colors::white)(esc_lt_white, Colors::lt_white);
}
