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

#ifndef UTILSTRING20200430H
#define UTILSTRING20200430H

#include <boost/algorithm/string.hpp>

namespace goby
{
namespace util
{
// thin wrapper around boost::split to avoid static analyzer warnings in Clang 10
// see https://bugs.llvm.org/show_bug.cgi?id=41141
template <typename SequenceSequenceT, typename RangeT, typename PredicateT>
SequenceSequenceT&
split(SequenceSequenceT& Result, RangeT& Input, PredicateT Pred,
      boost::algorithm::token_compress_mode_type eCompress = boost::algorithm::token_compress_off)
{
#ifndef __clang_analyzer__
    return boost::algorithm::split(Result, Input, Pred, eCompress);
#endif
}
} // namespace util
} // namespace goby

#endif
