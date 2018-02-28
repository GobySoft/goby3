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

#include "goby/moos/liaison_commander.h"
#include "goby/moos/liaison_scope.h"
#include "goby/moos/liaison_geodesy.h"
#include "goby/moos/liaison_acomms.h"

#include "moos_liaison_load.h"

std::vector<boost::shared_ptr<goby::common::ZeroMQService> > services_;

extern "C"
{    
    std::vector<goby::common::LiaisonContainer*> goby_liaison_load(const goby::common::protobuf::LiaisonConfig& cfg,
                                                                   boost::shared_ptr<zmq::context_t> zmq_context)
    {
        
        std::vector<goby::common::LiaisonContainer*> containers;

        services_.push_back(boost::shared_ptr<goby::common::ZeroMQService>(new goby::common::ZeroMQService(zmq_context)));
        containers.push_back(new goby::common::LiaisonCommander(services_.back().get(), cfg));

        services_.push_back(boost::shared_ptr<goby::common::ZeroMQService>(new goby::common::ZeroMQService(zmq_context)));
        containers.push_back(new goby::common::LiaisonScope(services_.back().get(), cfg));

        containers.push_back(new goby::common::LiaisonGeodesy(cfg));
        
        services_.push_back(boost::shared_ptr<goby::common::ZeroMQService>(new goby::common::ZeroMQService(zmq_context)));
        containers.push_back(new goby::common::LiaisonAcomms(services_.back().get(), cfg));

        return containers;
    }
}
