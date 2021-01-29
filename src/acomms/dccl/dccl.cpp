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

#include "dccl.h"

#include <boost/smart_ptr/shared_ptr.hpp> // for shared_ptr
#include <dccl/codec.h>                   // for Codec

#include "goby/util/debug_logger/term_color.h" // for Colors, Colors::lt_blue

std::string goby::acomms::DCCLCodec::glog_encode_group_ = "goby::acomms::dccl::encode";
std::string goby::acomms::DCCLCodec::glog_decode_group_ = "goby::acomms::dccl::decode";

goby::acomms::DCCLCodec::DCCLCodec() : codec_(new dccl::Codec)
{
    glog.add_group(glog_encode_group_, util::Colors::lt_magenta);
    glog.add_group(glog_decode_group_, util::Colors::lt_blue);

    if (!glog.buf().is_quiet())
    {
        dccl::logger::Verbosity verbosity = dccl::logger::ALL;

        switch (glog.buf().highest_verbosity())
        {
            default: break;
            case goby::util::logger::WARN: verbosity = dccl::logger::WARN_PLUS; break;
            case goby::util::logger::VERBOSE: verbosity = dccl::logger::INFO_PLUS; break;
            case goby::util::logger::DEBUG1: verbosity = dccl::logger::DEBUG1_PLUS; break;
            case goby::util::logger::DEBUG2: verbosity = dccl::logger::DEBUG2_PLUS; break;
            case goby::util::logger::DEBUG3: verbosity = dccl::logger::DEBUG3_PLUS; break;
        }
        dccl::dlog.connect(verbosity, this, &DCCLCodec::dlog_message);
    }
}
