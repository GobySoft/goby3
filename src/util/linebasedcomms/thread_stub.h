// Copyright 2020:
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

#ifndef GOBY_UTIL_LINEBASEDCOMMS_THREAD_STUB
#define GOBY_UTIL_LINEBASEDCOMMS_THREAD_STUB

#include "goby/middleware/application/thread.h"
#include "goby/middleware/transport/interthread.h"

namespace goby
{
namespace util
{
template <typename Config>
class LineBasedCommsThreadStub
    : public middleware::Thread<Config, middleware::InterThreadTransporter>
{
  public:
    LineBasedCommsThreadStub(const Config& cfg, double loop_freq_hertz, int index)
        : middleware::Thread<Config, middleware::InterThreadTransporter>(
              cfg, &interthread_, loop_freq_hertz * boost::units::si::hertz, index)
    {
    }
    ~LineBasedCommsThreadStub() = default;

    middleware::InterThreadTransporter& interthread() { return interthread_; }

  private:
    middleware::InterThreadTransporter interthread_;
};
} // namespace util
} // namespace goby

#endif
