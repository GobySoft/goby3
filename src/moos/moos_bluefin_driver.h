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

#ifndef BluefinCommsDriver20130412H
#define BluefinCommsDriver20130412H

#include "goby/moos/moos_header.h"

#include <boost/bimap.hpp>

#include "goby/common/time.h"

#include "goby/acomms/modemdriver/driver_base.h"
#include "goby/acomms/acomms_helpers.h"
#include "goby/acomms/amac.h"

#include "goby/moos/modem_id_convert.h"

namespace goby
{
    namespace moos
    {
        /// \brief provides a driver for the Bluefin Huxley communications infrastructure (initially uses SonarDyne as underlying hardware)
        /// \ingroup acomms_api
        /// 
        class BluefinCommsDriver : public goby::acomms::ModemDriverBase
        {
          public:
            BluefinCommsDriver(goby::acomms::MACManager* mac);
            void startup(const goby::acomms::protobuf::DriverConfig& cfg);
            void shutdown();            
            void do_work();
            void handle_initiate_transmission(const goby::acomms::protobuf::ModemTransmission& m);
    
          private:
            std::string unix_time2nmea_time(double time);
            void bfcma(const goby::util::NMEASentence& nmea);
            void bfcps(const goby::util::NMEASentence& nmea);
            void bfcpr(const goby::util::NMEASentence& nmea);
            
          private:
            CMOOSCommClient moos_client_;
            goby::acomms::protobuf::DriverConfig driver_cfg_; // configuration given to you at launch
            std::string current_modem_;
            double end_of_mac_window_;
            
            // modem name to map of rate to bytes
            std::map<std::string, std::map<int, int> > modem_to_rate_to_bytes_;

            // maps goby modem id to bluefin modem id
            boost::bimap<int, int> goby_to_bluefin_id_;
            
            goby::acomms::MACManager* mac_;

            int last_request_id_;
            goby::acomms::protobuf::ModemTransmission last_data_msg_;
        };
    }
}

#endif
