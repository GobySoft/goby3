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

#include "log.h"

std::map<int, boost::bimap<std::string, goby::uint<goby::LogEntry::group_bytes_>::type> >
    goby::LogEntry::groups_;
std::map<int, boost::bimap<std::string, goby::uint<goby::LogEntry::type_bytes_>::type> >
    goby::LogEntry::types_;

std::map<int, std::function<void(const std::string& type)> > goby::LogEntry::new_type_hook;
std::map<int, std::function<void(const goby::Group& group)> > goby::LogEntry::new_group_hook;

goby::uint<goby::LogEntry::group_bytes_>::type goby::LogEntry::group_index_(1);
goby::uint<goby::LogEntry::type_bytes_>::type goby::LogEntry::type_index_(1);
