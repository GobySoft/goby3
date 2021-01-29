// Copyright 2013-2021:
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

#ifndef GOBY_MIDDLEWARE_FRONTSEAT_EXCEPTION_H
#define GOBY_MIDDLEWARE_FRONTSEAT_EXCEPTION_H

#include "goby/exception.h"
#include "goby/middleware/protobuf/frontseat.pb.h"

namespace goby
{
namespace middleware
{
namespace frontseat
{
class Exception : goby::Exception
{
  public:
    Exception()
        : goby::Exception("Unknown FrontSeatException"),
          helm_err_(protobuf::ERROR_HELM_NONE),
          is_helm_error_(false),
          fs_err_(protobuf::ERROR_FRONTSEAT_NONE),
          is_fs_error_(false)
    {
    }

    Exception(protobuf::HelmError err)
        : goby::Exception(protobuf::HelmError_Name(err)),
          helm_err_(err),
          is_helm_error_(true),
          fs_err_(protobuf::ERROR_FRONTSEAT_NONE),
          is_fs_error_(false)
    {
    }
    Exception(protobuf::FrontSeatError err)
        : goby::Exception(protobuf::FrontSeatError_Name(err)),
          helm_err_(protobuf::ERROR_HELM_NONE),
          is_helm_error_(false),
          fs_err_(err),
          is_fs_error_(true)

    {
    }

    protobuf::HelmError helm_err() const { return helm_err_; }
    bool is_helm_error() const { return is_helm_error_; }

    protobuf::FrontSeatError fs_err() const { return fs_err_; }
    bool is_fs_error() const { return is_fs_error_; }

  private:
    protobuf::HelmError helm_err_;
    bool is_helm_error_;

    protobuf::FrontSeatError fs_err_;
    bool is_fs_error_;
};

inline std::ostream& operator<<(std::ostream& os, const goby::middleware::frontseat::Exception& e)
{
    if (e.is_helm_error())
        os << "Error in the Helm: " << protobuf::HelmError_Name(e.helm_err());
    else if (e.is_fs_error())
        os << "Error in the Frontseat: " << protobuf::FrontSeatError_Name(e.fs_err());
    else
        os << "Unknown error.";
    return os;
};
} // namespace frontseat
} // namespace middleware
} // namespace goby

#endif
