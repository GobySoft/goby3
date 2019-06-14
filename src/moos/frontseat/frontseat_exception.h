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

#ifndef FrontSeatException20130221H
#define FrontSeatException20130221H

#include "goby/moos/protobuf/frontseat.pb.h"

namespace goby
{
namespace moos
{
class FrontSeatException : std::runtime_error
{
  public:
    FrontSeatException()
        : std::runtime_error("Unknown FrontSeatException"),
          helm_err_(goby::moos::protobuf::ERROR_HELM_NONE), is_helm_error_(false),
          fs_err_(goby::moos::protobuf::ERROR_FRONTSEAT_NONE), is_fs_error_(false)
    {
    }

    FrontSeatException(goby::moos::protobuf::HelmError err)
        : std::runtime_error(goby::moos::protobuf::HelmError_Name(err)), helm_err_(err),
          is_helm_error_(true), fs_err_(goby::moos::protobuf::ERROR_FRONTSEAT_NONE),
          is_fs_error_(false)
    {
    }
    FrontSeatException(goby::moos::protobuf::FrontSeatError err)
        : std::runtime_error(goby::moos::protobuf::FrontSeatError_Name(err)),
          helm_err_(goby::moos::protobuf::ERROR_HELM_NONE), is_helm_error_(false), fs_err_(err),
          is_fs_error_(true)

    {
    }

    goby::moos::protobuf::HelmError helm_err() const { return helm_err_; }
    bool is_helm_error() const { return is_helm_error_; }

    goby::moos::protobuf::FrontSeatError fs_err() const { return fs_err_; }
    bool is_fs_error() const { return is_fs_error_; }

  private:
    goby::moos::protobuf::HelmError helm_err_;
    bool is_helm_error_;

    goby::moos::protobuf::FrontSeatError fs_err_;
    bool is_fs_error_;
};

inline std::ostream& operator<<(std::ostream& os, const goby::moos::FrontSeatException& e)
{
    if (e.is_helm_error())
        os << "Error in the Helm: " << goby::moos::protobuf::HelmError_Name(e.helm_err());
    else if (e.is_fs_error())
        os << "Error in the Frontseat: " << goby::moos::protobuf::FrontSeatError_Name(e.fs_err());
    else
        os << "Unknown error.";
    return os;
};
} // namespace moos
} // namespace goby

#endif
