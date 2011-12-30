// copyright 2010 t. schneider tes@mit.edu 
//
// this file is part of the Queue Library (libqueue),
// the goby-acomms message queue manager. goby-acomms is a collection of 
// libraries for acoustic underwater networking
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This software is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this software.  If not, see <http://www.gnu.org/licenses/>.

#ifndef QueueException20100812H
#define QueueException20100812H
// simple exception class

#include "goby/common/exception.h"

namespace goby
{
    namespace acomms
    {
        /// \brief Exception class for libdccl
        class QueueException : public goby::Exception
        {
          public:
          QueueException(const std::string& s)
              : Exception(s)
            { }
        };
    }
}

#endif
