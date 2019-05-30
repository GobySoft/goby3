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

#ifndef Modem20110225H
#define Modem20110225H

#include "goby/time.h"

#include "driver_base.h"
#include "goby/acomms/acomms_helpers.h"
#include "goby/acomms/protobuf/abc_driver.pb.h"

namespace goby
{
namespace acomms
{
/// \brief provides an API to the imaginary ABC modem (as an example how to write drivers)
/// \ingroup acomms_api
///
class ABCDriver : public ModemDriverBase
{
  public:
    ABCDriver();
    void startup(const protobuf::DriverConfig& cfg);
    void shutdown();
    void do_work();
    void handle_initiate_transmission(const protobuf::ModemTransmission& m);

  private:
    void parse_in(const std::string& in, std::map<std::string, std::string>* out);
    void signal_and_write(const std::string& raw);

  private:
    enum
    {
        DEFAULT_BAUD = 4800
    };

    protobuf::DriverConfig driver_cfg_; // configuration given to you at launch
    // rest is up to you!
};
} // namespace acomms
} // namespace goby
#endif
