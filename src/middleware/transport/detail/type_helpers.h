// Copyright 2009-2021:
//   GobySoft, LLC (2013-)
//   Massachusetts Institute of Technology (2007-2014)
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

#ifndef GOBY_MIDDLEWARE_TRANSPORT_DETAIL_TYPE_HELPERS_H
#define GOBY_MIDDLEWARE_TRANSPORT_DETAIL_TYPE_HELPERS_H

namespace goby
{
namespace middleware
{
namespace detail
{
template <typename Ret, typename Arg, typename... Rest>
Arg first_argument_helper(Ret (*)(Arg, Rest...));

template <typename Ret, typename F, typename Arg, typename... Rest>
Arg first_argument_helper(Ret (F::*)(Arg, Rest...));

template <typename Ret, typename F, typename Arg, typename... Rest>
Arg first_argument_helper(Ret (F::*)(Arg, Rest...) const);

template <typename F> decltype(first_argument_helper(&F::operator())) first_argument_helper(F);

// deduce the first argument for a variety of function-like things
template <typename T> using first_argument = decltype(first_argument_helper(std::declval<T>()));

// template <typename F> struct first_argument
// {
//     using type = decltype(first_argument_helper(std::declval<T>()));
//     using decayed_type = typename std::decay<type>::type;
// };

} // namespace detail
} // namespace middleware
} // namespace goby

#endif
