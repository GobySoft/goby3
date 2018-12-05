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

#ifndef FlexNCurses20091110H
#define FlexNCurses20091110H

#include <deque>
#include <string>
#include <vector>

#include <boost/thread.hpp>

#include "goby/common/time.h"
#include "term_color.h"

class Group;

namespace goby
{
namespace common
{
/// Enables the Verbosity == gui mode of the Goby logger and displays an NCurses gui for the logger content
class FlexNCurses
{
  public:
    /// \name Constructors / Destructor
    //@{
    FlexNCurses();
    ~FlexNCurses()
    {
        alive_ = false;
        cleanup();
    }
    //@}

    /// \name Initialization
    //@{
    void startup();
    void add_win(const Group* title);
    //@}

    /// \name Use
    //@{
    /// Add a string to the gui
    void insert(boost::posix_time::ptime t, const std::string& s, Group* g);
    //@}

    /// \name Utility
    //@{
    size_t panel_from_group(Group* g);
    void recalculate_win();
    void cleanup();
    void alive(bool alive);
    //@}

    /// \name User Input Thread
    //@{
    /// run in its own thread to take input from the user
    void run_input();
    //@}

  private:
    class Panel;
    void putline(const std::string& s, unsigned scrn, bool refresh = true);

    void putlines(unsigned scrn,
                  const std::multimap<boost::posix_time::ptime, std::string>::const_iterator& alpha,
                  const std::multimap<boost::posix_time::ptime, std::string>::const_iterator& omega,
                  bool refresh = true);

    void update_size();
    long color2attr_t(Colors::Color c);
    size_t find_containing_window(int y, int x);

    // void* is WINDOW*
    bool in_window(void* p, int y, int x);

    void write_head_title(size_t i);
    void deselect_all();
    void select_all();
    void select(size_t gt);
    void home();
    void end();

    size_t down() { return down(find_first_selected()); }
    size_t up() { return up(find_first_selected()); }
    size_t left() { return left(find_first_selected()); }
    size_t right() { return right(find_first_selected()); }
    size_t down(size_t curr);
    size_t up(size_t curr);
    size_t left(size_t curr);
    size_t right(size_t curr);
    size_t find_first_selected();
    void shift(size_t next);

    bool last_in_col(size_t val);
    bool first_in_col(size_t val);

    void move_up();
    void move_down();
    void move_right();
    void move_left();

    void combine();
    void uncombine_all();
    void uncombine(size_t i);
    void uncombine_selected();
    int lines_from_beg(int l, size_t i);

    void scroll_up();
    void scroll_down();
    void page_up();
    void page_down();
    void scroll_end();
    void scroll_home();

    void winlock();
    void winunlock();

    void grow_all();
    void shrink_all();
    void grow(int i);
    void shrink(int i);

    void restore_order();

    void toggle_minimized(int i);
    void redraw_lines(int j, int offset = -1);

    std::multimap<boost::posix_time::ptime, std::string> get_history(size_t i, int how_much = -1);
    size_t get_history_size(size_t i);

    template <typename T> T min(const T& a, const T& b) { return (a < b ? a : b); }

    template <typename T> T max(const T& a, const T& b) { return (a > b ? a : b); }

  private:
    int xmax_;
    int ymax_;

    int xwinN_;
    int ywinN_;

    int last_select_x_;
    int last_select_y_;

    // void* is WINDOW*
    std::vector<void*> vert_windows_;
    // bottom of the column (indexed by column)
    std::vector<void*> col_end_windows_;
    void* foot_window_;

    bool is_locked_;
    int locked_panel_;

    class Panel
    {
      public:
        Panel(const Group* group = 0)
            : group_(group), window_(0), head_window_(0), minimized_(false), selected_(false),
              locked_(false), col_(1), lines_from_beg_(0), original_order_(0), max_hist_(20000)
        {
        }

        void window(void* v) { window_ = v; }
        void head_window(void* v) { head_window_ = v; }
        void group(const Group* g) { group_ = g; }
        // returns number of lines to grow/shrink
        void set_minimized(bool b) { minimized_ = b; }
        int minimized(bool b);
        int toggle_minimized() { return minimized(minimized() ^ true); }
        void selected(bool b) { selected_ = b; }
        void ywidth(int i) { ywidth_ = i; }
        int lines_from_beg(int i);
        void grow() { ++ywidth_; }
        void locked(bool b) { locked_ = b; }
        bool shrink()
        {
            if (ywidth_ == HEAD_Y)
                return false;
            else
            {
                --ywidth_;
                return true;
            }
        }
        void col(int i) { col_ = i; }
        void original_order(int i) { original_order_ = i; }
        void add_combined(size_t i) { combined_.insert(i); }
        void clear_combined() { combined_.clear(); }

        void* window() const { return window_; }
        void* head_window() const { return head_window_; }
        const Group* group() const { return group_; }
        bool minimized() const { return minimized_; }
        bool selected() const { return selected_; }
        int ywidth() const { return ywidth_; }
        int col() const { return col_; }
        bool locked() const { return locked_; }
        int lines_from_beg() const { return lines_from_beg_; }
        int original_order() const { return original_order_; }
        std::multimap<boost::posix_time::ptime, std::string>& history() { return history_; }
        unsigned max_hist() const { return max_hist_; }
        const std::set<size_t>& combined() const { return combined_; }

      private:
        const Group* group_;
        void* window_;
        void* head_window_;
        bool minimized_;
        bool selected_;
        bool locked_;
        int ywidth_;
        int unminimized_ywidth_;
        int col_;
        int lines_from_beg_;
        int original_order_;
        unsigned max_hist_;
        std::multimap<boost::posix_time::ptime, std::string> history_;
        std::set<size_t> combined_;
    };

    std::vector<Panel> panels_;
    std::set<size_t> unique_panels_;

    // extra lines if everyone is minimized
    // indexed by column
    std::vector<int> line_buffer_;

    bool alive_;

    // ideal number of characters per line
    enum
    {
        CHARS_PER_LINE = 85
    };

    enum
    {
        HEAD_Y = 1
    };
    enum
    {
        FOOTER_Y = 3
    };
};
} // namespace common
} // namespace goby

#endif
