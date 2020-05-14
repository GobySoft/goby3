// Copyright 2019-2020:
//   GobySoft, LLC (2013-)
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

#include <algorithm>
#include <cmath>
#include <iostream>
#include <mutex>
#include <sstream>

#include <boost/algorithm/string.hpp>

#include <ncurses.h>

#include "goby/util/as.h"

#include "logger_manipulators.h"

#include "flex_ncurses.h"
#include "term_color.h"

using boost::posix_time::ptime;

// defined in flex_ostreambuf.cpp
extern std::mutex curses_mutex;

goby::util::FlexNCurses::FlexNCurses()
    : xmax_(0),
      ymax_(0),
      xwinN_(1),
      ywinN_(1),
      foot_window_(0),
      is_locked_(false),
      locked_panel_(0),
      alive_(true)
{
}

void goby::util::FlexNCurses::startup()
{
    initscr();
    start_color();

    init_pair(Colors::white, COLOR_WHITE, COLOR_BLACK);
    init_pair(Colors::red, COLOR_RED, COLOR_BLACK);
    init_pair(Colors::green, COLOR_GREEN, COLOR_BLACK);
    init_pair(Colors::yellow, COLOR_YELLOW, COLOR_BLACK);
    init_pair(Colors::blue, COLOR_BLUE, COLOR_BLACK);
    init_pair(Colors::magenta, COLOR_MAGENTA, COLOR_BLACK);
    init_pair(Colors::cyan, COLOR_CYAN, COLOR_BLACK);

    refresh();
    update_size();
    // special keys (KEY_RESIZE, KEY_LEFT, etc.)
    keypad(stdscr, TRUE);
    // no cursor
    curs_set(false);
    // tenths of seconds pause waiting for character input (getch)
    nodelay(stdscr, TRUE);
    halfdelay(1);
    noecho();
    // turn on mouse events

    // problems with locking screen
    //    mousemask(ALL_MOUSE_EVENTS, 0);
}

void goby::util::FlexNCurses::update_size()
{
    getmaxyx(stdscr, ymax_, xmax_);
    ymax_ -= FOOTER_Y;

    xwinN_ = std::max(1, xmax_ / CHARS_PER_LINE);
    ywinN_ = (panels_.size() % xwinN_) ? panels_.size() / xwinN_ + 1 : panels_.size() / xwinN_;

    line_buffer_.resize(xwinN_);
    for (size_t i : unique_panels_)
    {
        int col = (i / ywinN_);
        panels_[i].col(col);
        panels_[i].minimized(false);
        panels_[i].ywidth(ymax_ / ywinN_);
        line_buffer_[col] = ymax_;
        //int ywidth = (m_N == ywinN_) ? 0 : (ymax_-m_N*HEAD_Y)/(ywinN_-m_N);
    }

    for (size_t i : unique_panels_)
    {
        int col = (i / ywinN_);
        line_buffer_[col] -= panels_[i].ywidth();
    }
}

void goby::util::FlexNCurses::alive(bool alive) { alive_ = alive; }
void goby::util::FlexNCurses::cleanup()
{
    clear();
    refresh();
    endwin();
}

void goby::util::FlexNCurses::add_win(const logger::Group* g)
{
    int N_windows = panels_.size() + 1;

    if (xwinN_ * ywinN_ < N_windows)
        ++ywinN_;

    Panel new_panel = Panel(g);

    new_panel.original_order(panels_.size());
    unique_panels_.insert(panels_.size());
    panels_.push_back(new_panel);

    update_size();
    recalculate_win();
}

void goby::util::FlexNCurses::recalculate_win()
{
    // clear any old windows
    for (size_t i : unique_panels_)
    {
        delwin(static_cast<WINDOW*>(panels_[i].window()));
        delwin(static_cast<WINDOW*>(panels_[i].head_window()));
        panels_[i].window(0);
        panels_[i].head_window(0);
    }
    for (void* p : vert_windows_)
    {
        delwin(static_cast<WINDOW*>(p));
        p = 0;
    }
    for (void* p : col_end_windows_)
    {
        delwin(static_cast<WINDOW*>(p));
        p = 0;
    }
    delwin(static_cast<WINDOW*>(foot_window_));

    redrawwin(stdscr);
    refresh();

    foot_window_ = newwin(FOOTER_Y - 1, xmax_, ymax_ + 1, 0);
    mvwaddstr(static_cast<WINDOW*>(foot_window_), 0, 0,
              "help: [+]/[-]: expand/contract window | [w][a][s][d]: move window | spacebar: "
              "toggle minimize | [r]: reset | [CTRL][A]: select all | [1][2][3]...[n] select "
              "window n | [SHIFT][[n] select multiple | [enter] pause and scroll | [c]/[C] "
              "combine/uncombine selected windows");
    wrefresh(static_cast<WINDOW*>(foot_window_));

    col_end_windows_.resize(xwinN_);
    vert_windows_.resize(xwinN_ - 1);
    for (size_t i : unique_panels_)
    {
        int col = panels_[i].col();
        int ystart = 0;

        for (std::set<size_t>::iterator it = unique_panels_.begin(); (*it) < i; ++it)
        {
            if (panels_[*it].col() == col)
                ystart += panels_[*it].ywidth();
        }

        int ywidth = panels_[i].ywidth();
        int xwidth = xmax_ / xwinN_;
        int xstart = col * xwidth;

        // vertical divider
        // not last column, not if only one column
        if (col < xwinN_ - 1 && xwinN_ > 1)
        {
            WINDOW* new_vert = newwin(ymax_, 1, 0, (col + 1) * xwidth - 1);
            mvwvline(new_vert, 0, 0, 0, ymax_);
            wrefresh(new_vert);
            vert_windows_[col] = new_vert;
            --xwidth;
        }

        if (last_in_col(i))
        {
            WINDOW* new_col_end = newwin(1, xwidth, ystart + ywidth, xstart);
            col_end_windows_[col] = new_col_end;
            mvwhline(new_col_end, 0, 0, 0, xwidth);
            wrefresh(new_col_end);
        }

        // title and horizontal divider
        WINDOW* new_head = newwin(HEAD_Y, xwidth, ystart, xstart);
        panels_[i].head_window(new_head);
        for (size_t j : panels_[i].combined()) panels_[j].head_window(new_head);

        write_head_title(i);

        if (panels_[i].selected())
            select(i);

        if (ywidth > HEAD_Y)
        {
            // scrolling text window
            WINDOW* new_window = newwin(ywidth - HEAD_Y, xwidth, ystart + HEAD_Y, xstart);
            panels_[i].window(new_window);
            for (size_t j : panels_[i].combined()) panels_[j].window(new_window);

            // move the cursor to start place for text
            wmove(new_window, 0, 0);

            // set the background color
            wbkgd(new_window, COLOR_PAIR(Colors::white));

            // scrolling is good!
            scrollok(new_window, true);

            redraw_lines(i);

            // make it all stick
            wrefresh(new_window);
        }
    }
}

void goby::util::FlexNCurses::insert(ptime t, const std::string& s, logger::Group* g)
{
    size_t i = panel_from_group(g);

    std::multimap<ptime, std::string>& hist = panels_[i].history();
    if (hist.size() >= panels_[i].max_hist())
        hist.erase(hist.begin());

    //  first line shouldn't have newline at the start...
    if (!panels_[i].locked())
    {
        //  first line shouldn't have newline at the start...
        if (hist.empty())
            putline(boost::trim_left_copy(s), i);
        else
            putline(s, i);
    }
    hist.insert(std::pair<ptime, std::string>(t, s));
}

size_t goby::util::FlexNCurses::panel_from_group(logger::Group* g)
{
    for (size_t i = 0, n = panels_.size(); i < n; ++i)
    {
        if (panels_[i].group() == g)
            return i;
    }
    return 0;
}

void goby::util::FlexNCurses::putline(const std::string& s, unsigned scrn,
                                      bool refresh /* = true */)
{
    if (s.empty())
        return;

    if (scrn < panels_.size())
    {
        WINDOW* win = static_cast<WINDOW*>(panels_[scrn].window());
        if (!win)
            return;

        static const std::string esc = "\33[";
        static const std::string m = "m";

        auto length = s.length();
        decltype(length) esc_pos = std::string::npos, m_pos = std::string::npos, next_start_pos = 0;
        while ((esc_pos = s.find(esc, next_start_pos)) != std::string::npos &&
               (m_pos = s.find(m, esc_pos)) != std::string::npos)
        {
            std::string esc_sequence = s.substr(esc_pos, m_pos - esc_pos + 1);

            waddstr(win, s.substr(next_start_pos, esc_pos - next_start_pos).c_str());

            // void kills compiler warning and doesn't do anything bad
            (void)wattrset(win, color2attr_t(TermColor::from_esc_code(esc_sequence)));

            next_start_pos = m_pos + 1;
        }

        if (next_start_pos < length)
            waddstr(win, s.substr(next_start_pos).c_str());

        if (refresh)
            wrefresh(win);
    }
}

void goby::util::FlexNCurses::putlines(
    unsigned scrn, const std::multimap<ptime, std::string>::const_iterator& alpha,
    const std::multimap<ptime, std::string>::const_iterator& omega, bool refresh /* = true */)
{
    for (std::multimap<ptime, std::string>::const_iterator it = alpha; it != omega; ++it)
    {
        if (it == alpha)
            putline(boost::trim_left_copy(it->second), scrn, false);
        else
            putline(it->second, scrn, false);
    }

    if (refresh)
        wrefresh(static_cast<WINDOW*>(panels_[scrn].window()));
}

long goby::util::FlexNCurses::color2attr_t(Colors::Color c)
{
    switch (c)
    {
        default:
        case Colors::nocolor: return COLOR_PAIR(Colors::white);
        case Colors::red: return COLOR_PAIR(Colors::red);
        case Colors::lt_red: return COLOR_PAIR(Colors::red) | A_BOLD;
        case Colors::green: return COLOR_PAIR(Colors::green);
        case Colors::lt_green: return COLOR_PAIR(Colors::green) | A_BOLD;
        case Colors::yellow: return COLOR_PAIR(Colors::yellow);
        case Colors::lt_yellow: return COLOR_PAIR(Colors::yellow) | A_BOLD;
        case Colors::blue: return COLOR_PAIR(Colors::blue);
        case Colors::lt_blue: return COLOR_PAIR(Colors::blue) | A_BOLD;
        case Colors::magenta: return COLOR_PAIR(Colors::magenta);
        case Colors::lt_magenta: return COLOR_PAIR(Colors::magenta) | A_BOLD;
        case Colors::cyan: return COLOR_PAIR(Colors::cyan);
        case Colors::lt_cyan: return COLOR_PAIR(Colors::cyan) | A_BOLD;
        case Colors::white: return COLOR_PAIR(Colors::white);
        case Colors::lt_white: return COLOR_PAIR(Colors::white) | A_BOLD;
    }
}

// find screen containing click
size_t goby::util::FlexNCurses::find_containing_window(int y, int x)
{
    for (size_t i : unique_panels_)
    {
        if (in_window(panels_[i].window(), y, x) || in_window(panels_[i].head_window(), y, x))
            return i;
    }
    return panels_.size();
}

bool goby::util::FlexNCurses::in_window(void* p, int y, int x)
{
    if (!p)
        return false;

    int ybeg, xbeg;
    int ymax, xmax;
    getbegyx(static_cast<WINDOW*>(p), ybeg, xbeg);
    getmaxyx(static_cast<WINDOW*>(p), ymax, xmax);

    // for suppressing -Werror=unused-but-set-variable
    (void)ymax;

    return (y < ybeg + ymax && y >= ybeg && x < xbeg + xmax && x >= xbeg);
}

void goby::util::FlexNCurses::write_head_title(size_t i)
{
    WINDOW* win = static_cast<WINDOW*>(panels_[i].head_window());

    (void)wattrset(win, color2attr_t(util::Colors::lt_white));

    int ymax, xmax;
    getmaxyx(win, ymax, xmax);

    // for suppressing -Werror=unused-but-set-variable
    (void)ymax;

    mvwhline(win, 0, 0, 0, xmax);

    if (panels_[i].selected())
        wattron(win, A_REVERSE);
    else
        wattroff(win, A_REVERSE);

    attr_t color_attr = color2attr_t(panels_[i].group()->color());
    attr_t white_attr = color2attr_t(Colors::lt_white);
    wattron(win, white_attr);
    mvwaddstr(win, 0, 0, std::string(util::as<std::string>(i + 1) + ". ").c_str());

    std::stringstream ss;
    ss << panels_[i].group()->description() << " ";
    wattron(win, color_attr);
    waddstr(win, ss.str().c_str());

    for (size_t j : panels_[i].combined())
    {
        wattron(win, white_attr);
        waddstr(win, "| ");
        color_attr = color2attr_t(panels_[j].group()->color());
        waddstr(win, std::string(util::as<std::string>(j + 1) + ". ").c_str());

        std::stringstream ss_com;
        ss_com << panels_[j].group()->description() << " ";
        wattron(win, color_attr);
        waddstr(win, ss_com.str().c_str());
    }

    std::stringstream ss_tags;
    if (panels_[i].minimized())
        ss_tags << "(minimized) ";
    if (panels_[i].locked())
        ss_tags << "(paused, hit return to unlock) ";
    wattron(win, white_attr);
    waddstr(win, ss_tags.str().c_str());

    wrefresh(win);
}

void goby::util::FlexNCurses::deselect_all()
{
    if (is_locked_)
        return;

    for (size_t i : unique_panels_)
    {
        if (panels_[i].selected())
        {
            panels_[i].selected(false);
            write_head_title(i);
        }
    }
}

void goby::util::FlexNCurses::select_all()
{
    for (size_t i : unique_panels_) select(i);
}

void goby::util::FlexNCurses::select(size_t gt)
{
    if (is_locked_)
        return;

    if (gt < panels_.size())
    {
        panels_[gt].selected(true);
        write_head_title(gt);
    }
}

size_t goby::util::FlexNCurses::down(size_t curr)
{
    int ybeg, xbeg;
    int ymax, xmax;
    getbegyx(static_cast<WINDOW*>(panels_[curr].head_window()), ybeg, xbeg);
    getmaxyx(static_cast<WINDOW*>(panels_[curr].window()), ymax, xmax);
    size_t next = find_containing_window(ybeg + ymax + HEAD_Y + 1, xbeg);

    return next;
}

size_t goby::util::FlexNCurses::up(size_t curr)
{
    int ybeg, xbeg;
    getbegyx(static_cast<WINDOW*>(panels_[curr].head_window()), ybeg, xbeg);
    size_t next = find_containing_window(ybeg - 1, xbeg);
    return next;
}

size_t goby::util::FlexNCurses::left(size_t curr)
{
    int ybeg, xbeg;
    getbegyx(static_cast<WINDOW*>(panels_[curr].head_window()), ybeg, xbeg);
    // cross the divider
    size_t next = find_containing_window(ybeg, xbeg - 2);
    return next;
}

size_t goby::util::FlexNCurses::right(size_t curr)
{
    int ybeg, xbeg;
    int ymax, xmax;
    getbegyx(static_cast<WINDOW*>(panels_[curr].head_window()), ybeg, xbeg);
    getmaxyx(static_cast<WINDOW*>(panels_[curr].head_window()), ymax, xmax);

    // for suppressing -Werror=unused-but-set-variable
    (void)ymax;

    // cross the divider
    size_t next = find_containing_window(ybeg, xbeg + xmax + 2);
    return next;
}

void goby::util::FlexNCurses::home() { shift(0); }

void goby::util::FlexNCurses::end() { shift(panels_.size() - 1); }

void goby::util::FlexNCurses::shift(size_t next)
{
    if (next < panels_.size())
    {
        deselect_all();
        select(next);
    }
}

void goby::util::FlexNCurses::combine()
{
    size_t lowest = 0;
    for (size_t i : unique_panels_)
    {
        if (panels_[i].selected())
        {
            lowest = i;
            break;
        }
    }

    for (size_t i : unique_panels_)
    {
        if (panels_[i].selected() && i != lowest)
        {
            panels_[lowest].add_combined(i);
            for (size_t j : panels_[i].combined()) panels_[lowest].add_combined(j);

            panels_[i].clear_combined();

            unique_panels_.erase(i);
        }
    }

    update_size();
    recalculate_win();
}

void goby::util::FlexNCurses::uncombine(size_t i)
{
    for (size_t j : panels_[i].combined()) unique_panels_.insert(j);
    panels_[i].clear_combined();
}

void goby::util::FlexNCurses::uncombine_selected()
{
    for (size_t i : unique_panels_)
    {
        if (panels_[i].selected())
        {
            uncombine(i);
        }
    }

    update_size();
    recalculate_win();
}

void goby::util::FlexNCurses::uncombine_all()
{
    for (size_t i : unique_panels_) uncombine(i);

    update_size();
    recalculate_win();
}

void goby::util::FlexNCurses::move_up()
{
    for (size_t i : unique_panels_)
    {
        if (!first_in_col(i) && panels_[i].selected())
            iter_swap(panels_.begin() + i, panels_.begin() + i - 1);
    }
    recalculate_win();
}

void goby::util::FlexNCurses::move_down()
{
    //    BOOST_REVERSE_FOREACH(size_t i, unique_panels_)
    for (std::set<size_t>::reverse_iterator it = unique_panels_.rbegin(), n = unique_panels_.rend();
         it != n; ++it)
    {
        size_t i = *it;
        if (!last_in_col(i) && panels_[i].selected())
            iter_swap(panels_.begin() + i, panels_.begin() + i + 1);
    }
    recalculate_win();
}

void goby::util::FlexNCurses::move_right()
{
    //    BOOST_REVERSE_FOREACH(size_t i, unique_panels_)
    for (std::set<size_t>::reverse_iterator it = unique_panels_.rbegin(), n = unique_panels_.rend();
         it != n; ++it)
    {
        size_t i = *it;
        size_t rpanel = right(i);
        if (rpanel == panels_.size())
        {
            for (size_t j : unique_panels_)
            {
                if (last_in_col(j) && panels_[i].col() + 1 == panels_[j].col())
                    rpanel = j + 1;
            }
        }

        int old_col = panels_[i].col();
        if (panels_[i].selected() && old_col < xwinN_ - 1)
        {
            panels_[i].col(old_col + 1);
            Panel p = panels_[i];
            int orig_width = p.ywidth();
            line_buffer_[old_col] += orig_width;
            p.ywidth(0);
            panels_.insert(panels_.begin() + rpanel, p);
            panels_.erase(panels_.begin() + i);
            for (int j = 0, m = orig_width; j < m; ++j) grow(rpanel - 1);
        }
    }

    recalculate_win();
}

void goby::util::FlexNCurses::move_left()
{
    for (size_t i : unique_panels_)
    {
        size_t lpanel = left(i);
        if (lpanel == panels_.size())
        {
            for (size_t j : unique_panels_)
            {
                if (first_in_col(j) && panels_[i].col() == panels_[j].col())
                    lpanel = j;
            }
        }

        int old_col = panels_[i].col();
        if (panels_[i].selected() && old_col > 0)
        {
            panels_[i].col(old_col - 1);
            Panel p = panels_[i];
            int orig_width = p.ywidth();
            line_buffer_[old_col] += orig_width;
            p.ywidth(0);
            panels_.erase(panels_.begin() + i);
            panels_.insert(panels_.begin() + lpanel, p);

            for (int j = 0, m = orig_width; j < m; ++j) grow(lpanel);
        }
    }
    recalculate_win();
}

size_t goby::util::FlexNCurses::find_first_selected()
{
    for (size_t i : unique_panels_)
    {
        if (panels_[i].selected())
            return i;
    }
    return 0;
}

bool goby::util::FlexNCurses::last_in_col(size_t val)
{
    for (size_t i : unique_panels_)
    {
        if (panels_[i].col() == panels_[val].col() && i > val)
            return false;
    }
    return true;
}

bool goby::util::FlexNCurses::first_in_col(size_t val)
{
    for (size_t i : unique_panels_)
    {
        if (panels_[i].col() == panels_[val].col() && i < val)
            return false;
    }
    return true;
}

void goby::util::FlexNCurses::grow_all()
{
    for (size_t i : unique_panels_)
    {
        if (panels_[i].selected())
            grow(i);
    }
    recalculate_win();
}

void goby::util::FlexNCurses::shrink_all()
{
    for (size_t i : unique_panels_)
    {
        if (panels_[i].selected())
            shrink(i);
    }
    recalculate_win();
}

void goby::util::FlexNCurses::grow(int i)
{
    panels_[i].set_minimized(false);
    size_t largest_panel = panels_.size();
    int largest_panel_size = 0;
    for (size_t j : unique_panels_)
    {
        if (panels_[j].ywidth() >= largest_panel_size && panels_[j].col() == panels_[i].col() &&
            !panels_[j].minimized() && !panels_[j].selected())
        {
            largest_panel_size = panels_[j].ywidth();
            largest_panel = j;
        }
    }

    // shrink the line buffer
    if (line_buffer_[panels_[i].col()])
    {
        panels_[i].grow();
        --line_buffer_[panels_[i].col()];
    }
    // shrink the largest panel
    else if (largest_panel < panels_.size() && panels_[largest_panel].shrink())
    {
        panels_[i].grow();
    }
}

void goby::util::FlexNCurses::shrink(int i)
{
    size_t smallest_panel = panels_.size();
    int smallest_panel_size = ymax_;
    for (size_t j : unique_panels_)
    {
        if (panels_[j].ywidth() <= smallest_panel_size && panels_[j].col() == panels_[i].col() &&
            !panels_[j].minimized() && !panels_[j].selected())
        {
            smallest_panel_size = panels_[j].ywidth();
            smallest_panel = j;
        }
    }
    // shrink the panel...
    if (panels_[i].shrink())
    {
        // and add to the the smallest panel
        if (smallest_panel < panels_.size())
            panels_[smallest_panel].grow();
        // or add to the line buffer
        else
            ++line_buffer_[panels_[i].col()];
    }
}

void goby::util::FlexNCurses::toggle_minimized(int i)
{
    int change = panels_[i].toggle_minimized();
    for (int j = 0, m = abs(change); j < m; ++j) (change / abs(change) == 1) ? grow(i) : shrink(i);
}

void goby::util::FlexNCurses::winunlock()
{
    for (size_t j : unique_panels_)
    {
        if (panels_[j].locked())
        {
            panels_[j].locked(false);
            for (size_t k : panels_[j].combined()) panels_[k].locked(false);

            write_head_title(j);
            redraw_lines(j);
            lines_from_beg(0, j);
        }
    }

    is_locked_ = false;
}

void goby::util::FlexNCurses::redraw_lines(int j, int offset /* = -1 */)
{
    wclear(static_cast<WINDOW*>(panels_[j].window()));

    if (offset < 0)
    {
        const std::multimap<ptime, std::string>& hist = get_history(j, panels_[j].ywidth());
        int past = min(static_cast<int>(hist.size()), panels_[j].ywidth() - 1);

        std::multimap<ptime, std::string>::const_iterator a_it = hist.end();
        for (int k = 0; k < past; ++k) --a_it;

        putlines(j, a_it, hist.end());
    }
    else
    {
        const std::multimap<ptime, std::string>& hist = get_history(j);
        int past = min(static_cast<int>(hist.size()), panels_[j].ywidth() - 1);

        int eff_offset = min(static_cast<int>(hist.size()) - past, offset);

        std::multimap<ptime, std::string>::const_iterator a_it = hist.begin();
        for (int k = 0; k < eff_offset; ++k) ++a_it;

        std::multimap<ptime, std::string>::const_iterator o_it = a_it;
        for (int k = 0; k < past; ++k) ++o_it;

        putlines(j, a_it, o_it);
    }
}

void goby::util::FlexNCurses::winlock()
{
    size_t i = panels_.size();
    for (size_t j : unique_panels_)
    {
        if (panels_[j].selected())
        {
            i = j;
            break;
        }
    }
    if (i == panels_.size())
        return;

    deselect_all();
    select(i);

    is_locked_ = true;
    locked_panel_ = i;
    panels_[i].locked(true);
    for (size_t j : panels_[i].combined()) panels_[j].locked(true);

    lines_from_beg(get_history_size(i) - panels_[i].ywidth(), i);
    write_head_title(i);
}

void goby::util::FlexNCurses::scroll_up()
{
    int i = locked_panel_;
    int l = panels_[i].lines_from_beg();
    redraw_lines(i, lines_from_beg(l - 1, i));
}

void goby::util::FlexNCurses::scroll_down()
{
    int i = locked_panel_;
    int l = panels_[i].lines_from_beg();
    redraw_lines(i, lines_from_beg(l + 1, i));
}

void goby::util::FlexNCurses::page_up()
{
    int i = locked_panel_;
    int l = panels_[i].lines_from_beg();
    redraw_lines(i, lines_from_beg(l - (panels_[i].ywidth() - 1), i));
}

void goby::util::FlexNCurses::page_down()
{
    int i = locked_panel_;
    int l = panels_[i].lines_from_beg();
    redraw_lines(i, lines_from_beg(l + (panels_[i].ywidth() - 1), i));
}
void goby::util::FlexNCurses::scroll_end()
{
    int i = locked_panel_;
    redraw_lines(i, lines_from_beg(get_history_size(i), i));
}
void goby::util::FlexNCurses::scroll_home()
{
    int i = locked_panel_;
    redraw_lines(i, lines_from_beg(0, i));
}

void goby::util::FlexNCurses::restore_order()
{
    std::vector<Panel> new_panels;
    new_panels.resize(panels_.size());

    for (const Panel& p : panels_) { new_panels[p.original_order()] = p; }

    panels_ = new_panels;
}

std::multimap<ptime, std::string> goby::util::FlexNCurses::get_history(size_t i,
                                                                       int how_much /* = -1 */)
{
    if (panels_[i].combined().empty())
        return panels_[i].history();
    else
    {
        std::multimap<ptime, std::string>::iterator i_it_begin;
        if (how_much < 0)
            i_it_begin = panels_[i].history().begin();
        else
        {
            i_it_begin = panels_[i].history().end();
            for (int k = 0; k < how_much && i_it_begin != panels_[i].history().begin(); ++k)
                --i_it_begin;
        }

        std::multimap<ptime, std::string> merged;
        for (; i_it_begin != panels_[i].history().end(); ++i_it_begin) merged.insert(*i_it_begin);

        for (size_t j : panels_[i].combined())
        {
            std::multimap<ptime, std::string>::iterator j_it_begin;
            if (how_much < 0)
                j_it_begin = panels_[j].history().begin();
            else
            {
                j_it_begin = panels_[j].history().end();
                for (int k = 0; k < how_much && j_it_begin != panels_[j].history().begin(); ++k)
                    --j_it_begin;
            }

            for (; j_it_begin != panels_[j].history().end(); ++j_it_begin)
                merged.insert(*j_it_begin);
        }
        return merged;
    }
}

size_t goby::util::FlexNCurses::get_history_size(size_t i)
{
    if (panels_[i].combined().empty())
        return panels_[i].history().size();
    else
    {
        size_t sum = panels_[i].history().size();
        for (size_t j : panels_[i].combined()) { sum += panels_[j].history().size(); }
        return sum;
    }
}

int goby::util::FlexNCurses::lines_from_beg(int l, size_t i)
{
    int hist_size = get_history_size(i);
    int past = std::min(hist_size, panels_[i].ywidth());
    if (l <= 0)
        return panels_[i].lines_from_beg(0);
    else if (l >= hist_size - past + 1)
        return panels_[i].lines_from_beg(hist_size - past + 1);
    else
        return panels_[i].lines_from_beg(l);
}

int goby::util::FlexNCurses::Panel::lines_from_beg(int i) { return lines_from_beg_ = i; }

int goby::util::FlexNCurses::Panel::minimized(bool b)
{
    minimized_ = b;
    if (b)
    {
        unminimized_ywidth_ = ywidth_;
        return HEAD_Y - unminimized_ywidth_;
    }
    else
    {
        return unminimized_ywidth_ - HEAD_Y;
    }
}

void goby::util::FlexNCurses::run_input()
{
    while (alive_)
    {
        int k = getch();

        std::lock_guard<std::mutex> lock(curses_mutex);
        switch (k)
        {
            // same as resize but restores the order too
            case 'r': uncombine_all(); restore_order();
            case KEY_RESIZE:
                update_size();
                recalculate_win();
                break;

            case '1':
                deselect_all();
                select(0);
                break;
            case '2':
                deselect_all();
                select(1);
                break;
            case '3':
                deselect_all();
                select(2);
                break;
            case '4':
                deselect_all();
                select(3);
                break;
            case '5':
                deselect_all();
                select(4);
                break;
            case '6':
                deselect_all();
                select(5);
                break;
            case '7':
                deselect_all();
                select(6);
                break;
            case '8':
                deselect_all();
                select(7);
                break;
            case '9':
                deselect_all();
                select(8);
                break;
            case '0':
                deselect_all();
                select(9);
                break;

                // shift + numbers
            case '!': select(0); break;
            case '@': select(1); break;
            case '#': select(2); break;
            case '$': select(3); break;
            case '%': select(4); break;
            case '^': select(5); break;
            case '&': select(6); break;
            case '*': select(7); break;
            case '(': select(8); break;
            case ')': select(9); break;

            case 'a': move_left(); break;
            case 'd': move_right(); break;
            case 'w': move_up(); break;
            case 's': move_down(); break;

            case '+':
            case '=': grow_all(); break;
            case '_':
            case '-': shrink_all(); break;

            case 'c': combine(); break;
            case 'C': uncombine_selected(); break;

            case 'm':
            case ' ':
                for (size_t i : unique_panels_)
                {
                    if (panels_[i].selected())
                        toggle_minimized(i);
                }
                recalculate_win();
                break;

            case 'M':
                for (size_t i : unique_panels_)
                {
                    if (!panels_[i].selected())
                        toggle_minimized(i);
                }
                recalculate_win();
                break;

            case 'D':
                deselect_all();
                break;
                // CTRL-A
            case 1: select_all(); break;

            case '\n':
            case KEY_ENTER: (is_locked_) ? winunlock() : winlock(); break;

            case KEY_LEFT: shift(left()); break;
            case KEY_RIGHT: shift(right()); break;
            case KEY_DOWN: (!is_locked_) ? shift(down()) : scroll_down(); break;
            case KEY_UP: (!is_locked_) ? shift(up()) : scroll_up(); break;

            case KEY_PPAGE:
                if (is_locked_)
                    page_up();
                break;

            case KEY_NPAGE:
                if (is_locked_)
                    page_down();
                break;

            case KEY_END: (!is_locked_) ? end() : scroll_end(); break;
            case KEY_HOME: (!is_locked_) ? home() : scroll_home(); break;

            case KEY_MOUSE:
                MEVENT mort;
                getmouse(&mort);
                size_t gt = find_containing_window(mort.y, mort.x);
                if (gt >= panels_.size())
                    break;

                switch (mort.bstate)
                {
                    case BUTTON1_CLICKED:
                    case BUTTON1_PRESSED:
                        deselect_all();
                        select(gt);
                        last_select_x_ = mort.x;
                        last_select_y_ = mort.y;
                        break;
                    case BUTTON1_RELEASED:
                        for (int x = min(mort.x, last_select_x_), n = max(mort.x, last_select_x_);
                             x < n; ++x)
                        {
                            for (size_t y = min(mort.y, last_select_y_),
                                        m = max(mort.y, last_select_y_);
                                 y < m; ++y)
                            {
                                size_t t = find_containing_window(y, x);
                                if (!panels_[t].selected())
                                    select(t);
                            }
                        }

                        break;
                    case BUTTON1_DOUBLE_CLICKED:
                        toggle_minimized(gt);
                        recalculate_win();
                        break;
                }
        }
        refresh();
    }
}
