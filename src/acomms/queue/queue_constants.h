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

#ifndef QueueConstants20091205H
#define QueueConstants20091205H

#include "goby/common/time.h"
#include "goby/acomms/acomms_constants.h"

namespace goby
{
    namespace acomms
    {
        const unsigned MULTIMESSAGE_MASK = 1 << 7;
        const unsigned BROADCAST_MASK = 1 << 6;
        const unsigned VAR_ID_MASK = 0xFF ^ MULTIMESSAGE_MASK ^ BROADCAST_MASK;

        // number of bytes used to store the size of the following user frame
        const unsigned USER_FRAME_NEXT_SIZE_BYTES = 1;
        
        // how old an on_demand message can be before re-encoding
        const boost::posix_time::time_duration ON_DEMAND_SKEW = boost::posix_time::seconds(1);    
    }
}
#endif
