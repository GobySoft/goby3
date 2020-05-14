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

#ifndef ThreadTypeSelector20191105H
#define ThreadTypeSelector20191105H

#include <memory>

namespace goby
{
namespace middleware
{
namespace detail
{
/// \brief Selects which constructor to use based on whether the thread is launched with an index or not (that is, index == -1). Not directly called by user code.
template <typename ThreadType, typename ThreadConfig, bool has_index> struct ThreadTypeSelector
{
};

/// \brief ThreadTypeSelector instantiation for calling a constructor \e without an index parameter, e.g. "MyThread(const MyConfig& cfg)"
template <typename ThreadType, typename ThreadConfig>
struct ThreadTypeSelector<ThreadType, ThreadConfig, false>
{
    static std::shared_ptr<ThreadType> thread(const ThreadConfig& cfg, int index = -1)
    {
        return std::make_shared<ThreadType>(cfg);
    };
};

/// \brief ThreadTypeSelector instantiation for calling a constructor \e with an index parameter, e.g. "MyThread(const MyConfig& cfg, int index)"
template <typename ThreadType, typename ThreadConfig>
struct ThreadTypeSelector<ThreadType, ThreadConfig, true>
{
    static std::shared_ptr<ThreadType> thread(const ThreadConfig& cfg, int index)
    {
        return std::make_shared<ThreadType>(cfg, index);
    };
};
} // namespace detail
} // namespace middleware
} // namespace goby

#endif
