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

#ifndef GOBYHDFPREDICATE520160525H
#define GOBYHDFPREDICATE520160525H

#include "H5Cpp.h"

#include "goby/util/primitive_types.h"

namespace goby
{
    namespace common
    {
        namespace hdf5
        {
            template <typename T> H5::PredType predicate();
            template<> H5::PredType predicate<goby::int32>() { return H5::PredType::NATIVE_INT32; }
            template<> H5::PredType predicate<goby::int64>() { return H5::PredType::NATIVE_INT64; }
            template<> H5::PredType predicate<goby::uint32>() { return H5::PredType::NATIVE_UINT32; }
            template<> H5::PredType predicate<goby::uint64>() { return H5::PredType::NATIVE_UINT64; }
            template<> H5::PredType predicate<float>() { return H5::PredType::NATIVE_FLOAT; }
            template<> H5::PredType predicate<double>() { return H5::PredType::NATIVE_DOUBLE; }
            template<> H5::PredType predicate<unsigned char>() { return H5::PredType::NATIVE_UCHAR; }
        }
    }
}


#endif
