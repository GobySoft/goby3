// Copyright 2011-2020:
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

#ifndef GOBYHDFPREDICATE520160525H
#define GOBYHDFPREDICATE520160525H

#include "H5Cpp.h"

#include "goby/util/primitive_types.h"

namespace goby
{
namespace middleware
{
namespace hdf5
{
template <typename T> H5::PredType predicate();
template <> H5::PredType predicate<std::int32_t>() { return H5::PredType::NATIVE_INT32; }
template <> H5::PredType predicate<std::int64_t>() { return H5::PredType::NATIVE_INT64; }
template <> H5::PredType predicate<std::uint32_t>() { return H5::PredType::NATIVE_UINT32; }
template <> H5::PredType predicate<std::uint64_t>() { return H5::PredType::NATIVE_UINT64; }
template <> H5::PredType predicate<float>() { return H5::PredType::NATIVE_FLOAT; }
template <> H5::PredType predicate<double>() { return H5::PredType::NATIVE_DOUBLE; }
template <> H5::PredType predicate<unsigned char>() { return H5::PredType::NATIVE_UCHAR; }
} // namespace hdf5
} // namespace middleware
} // namespace goby

#endif
