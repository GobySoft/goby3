// Copyright 2009-2018 Toby Schneider (http://gobysoft.org/index.wt/people/toby)
//                     GobySoft, LLC (2013-)
//                     Massachusetts Institute of Technology (2007-2014)
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

#include <cctype>
#include <sstream>
#include <string>

#include <boost/lexical_cast.hpp>

#include "chat_curses.h"

void ChatCurses::startup()
{
    initscr();
    start_color();

    refresh();
    update_size();
    keypad(stdscr, TRUE);
    curs_set(false);

    // tenths of seconds pause waiting for character input (getch)
    nodelay(stdscr, TRUE);
    halfdelay(1);

    noecho();
}

void ChatCurses::update_size()
{
    getmaxyx(stdscr, ymax_, xmax_);

    delwin(upper_win_);
    delwin(lower_win_);
    delwin(divider_win_);

    upper_win_ = newwin(ymax_ * UPPER_WIN_FRAC - 1, xmax_, 0, 0);
    divider_win_ = newwin(1, xmax_, ymax_ * UPPER_WIN_FRAC - 1, 0);
    lower_win_ = newwin(ymax_ * LOWER_WIN_FRAC, xmax_, ymax_ * UPPER_WIN_FRAC, 0);

    scrollok(upper_win_, true);

    mvwhline(divider_win_, 0, 0, 0, xmax_);
    wrefresh(divider_win_);
}

void ChatCurses::run_input(std::string& line)
{
    chtype k = getch();

    if (k == KEY_DC || k == KEY_BACKSPACE)
    {
        if (!line_buffer_.empty())
            line_buffer_.resize(line_buffer_.size() - 1);
        wclear(lower_win_);
        waddstr(lower_win_, line_buffer_.c_str());
    }
    else if ((k == KEY_ENTER || k == '\n') && !line_buffer_.empty())
    {
        line = line_buffer_;
        wclear(lower_win_);
        post_message(id_, line_buffer_);
        line_buffer_.clear();
    }
    else if (k == KEY_RESIZE)
    {
        update_size();
    }
    else if (isprint(k) && line_buffer_.length() < MAX_LINE)
    {
        waddch(lower_win_, k);
        line_buffer_ += k;
    }

    wrefresh(lower_win_);
}

void ChatCurses::cleanup() { endwin(); }

void ChatCurses::post_message(unsigned id, const std::string& line)
{
    std::stringstream ss;
    ss << "[" << id << "]: " << line;
    post_message(ss.str());
}

void ChatCurses::post_message(const std::string& line)
{
    std::string line_plus_end = line + "\n";
    waddstr(upper_win_, line_plus_end.c_str());
    wrefresh(upper_win_);
}
