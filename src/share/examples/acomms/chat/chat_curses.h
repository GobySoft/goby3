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

#include <ncurses.h>

const float UPPER_WIN_FRAC = 0.75;
const float LOWER_WIN_FRAC = 1-UPPER_WIN_FRAC;
const unsigned MAX_LINE = 26; // fixed in chat.xml

/// provides a terminal GUI for a chat window (lower box to type and upper box to receive messages). Part of the chat.cpp example.
class ChatCurses
{
  public:
        /// \name Constructors/Destructor
        //@{
    ChatCurses() {}
    ~ChatCurses()
    { cleanup(); }
    //@}

    /// give the modem_id so we know how to label our messages
    void set_modem_id(unsigned id) { id_ = id; }    

    /// start the display
    void startup();
    /// grab a character and if there's a line to return it will be returned in \c line
    void run_input(std::string& line);

    /// end the display
    void cleanup();

    /// add a message to the upper window (the chat log)
    void post_message(unsigned id, const std::string& line);
    void post_message(const std::string& line);
    

  private:
    void update_size();


    
  private:
    int xmax_;
    int ymax_;

    unsigned id_;
    
    WINDOW* upper_win_;
    WINDOW* lower_win_;
    WINDOW* divider_win_;

    std::string line_buffer_;
};


