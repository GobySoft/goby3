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

#ifndef pAcommsHandlerH
#define pAcommsHandlerH

#include <iostream>
#include <fstream>

#include <google/protobuf/descriptor.h>

#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/asio/deadline_timer.hpp>
#include <boost/bimap.hpp>

#include "goby/acomms.h"
#include "goby/util.h"

#ifdef ENABLE_GOBY_V1_TRANSITIONAL_SUPPORT
#include "goby/moos/transitional/dccl_transitional.h"
#endif

#include "goby/util/dynamic_protobuf_manager.h"
#include "goby/moos/goby_moos_app.h"
#include "goby/moos/moos_translator.h"
#include "goby/common/zeromq_service.h"

#include "goby/moos/protobuf/pAcommsHandler_config.pb.h"
#include "goby/acomms/modemdriver/driver_exception.h"


namespace goby {
    namespace acomms {
        namespace protobuf {
            class ModemDataTransmission;
        }
    } 
}

namespace boost 
{
    inline bool operator<(const shared_ptr<goby::acomms::ModemDriverBase>& lhs, const shared_ptr<goby::acomms::ModemDriverBase>& rhs)
    {
        int lhs_count = lhs ? lhs->driver_order() : 0;
        int rhs_count = rhs ? rhs->driver_order() : 0;
        
        return lhs_count < rhs_count;
    }
}

        

class CpAcommsHandler : public GobyMOOSApp
{
  public:
    static CpAcommsHandler* get_instance();
    static void delete_instance();
    static std::map<std::string, void*> driver_plugins_;

  private:
    typedef boost::asio::basic_deadline_timer<goby::common::GobyTime> Timer;
    
    CpAcommsHandler();
    ~CpAcommsHandler();
    void loop();     // from GobyMOOSApp    
        
    
    void process_configuration();
    void create_driver(boost::shared_ptr<goby::acomms::ModemDriverBase>& driver,
                       goby::acomms::protobuf::DriverType driver_type,
                       goby::acomms::protobuf::DriverConfig* driver_cfg,
                       goby::acomms::MACManager* mac);
    
    void create_on_publish(const CMOOSMsg& trigger_msg, const goby::moos::protobuf::TranslatorEntry& entry);
    void create_on_multiplex_publish(const CMOOSMsg& moos_msg);
    void create_on_timer(const boost::system::error_code& error,
                         const goby::moos::protobuf::TranslatorEntry& entry,
                         Timer* timer);
    
    void translate_and_push(const goby::moos::protobuf::TranslatorEntry& entry);    
    
    // from QueueManager
    void handle_queue_receive(const google::protobuf::Message& msg);

    void handle_goby_signal(const google::protobuf::Message& msg1,
                            const std::string& moos_var1,
                            const google::protobuf::Message& msg2,
                            const std::string& moos_var2);

    void handle_raw(const goby::acomms::protobuf::ModemRaw& msg, const std::string& moos_var);

    void handle_mac_cycle_update(const CMOOSMsg& msg);
    void handle_flush_queue(const CMOOSMsg& msg);
    void handle_external_initiate_transmission(const CMOOSMsg& msg);

    void handle_config_file_request(const CMOOSMsg& msg);

    void handle_driver_reset(const CMOOSMsg& msg);

    void handle_driver_cfg_update(const goby::acomms::protobuf::DriverConfig& cfg);
    
    void handle_encode_on_demand(const goby::acomms::protobuf::ModemTransmission& request_msg,
                                 google::protobuf::Message* data_msg);

    void driver_bind();
    void driver_unbind();
    void driver_reset(boost::shared_ptr<goby::acomms::ModemDriverBase> driver,
                      const goby::acomms::ModemDriverException& e,
                      pAcommsHandlerConfig::DriverFailureApproach::DriverFailureTechnique = cfg_.driver_failure_approach().technique());
    
    void restart_drivers();
    
    enum { ALLOWED_TIMER_SKEW_SECONDS = 1 };
    
  private:
    goby::moos::MOOSTranslator translator_;
    
#ifdef ENABLE_GOBY_V1_TRANSITIONAL_SUPPORT
    //Old style XML DCCL1 parsing
    goby::transitional::DCCLTransitionalCodec transitional_dccl_;
#endif
    
    // new DCCL2 codec
    goby::acomms::DCCLCodec* dccl_;
    
    // manages queues and does additional packing
    goby::acomms::QueueManager queue_manager_;
    
    // driver class that interfaces to the modem
    boost::shared_ptr<goby::acomms::ModemDriverBase> driver_;
    
    // driver and additional listener drivers (receive only)
    std::map<boost::shared_ptr<goby::acomms::ModemDriverBase>, goby::acomms::protobuf::DriverConfig* > drivers_;    
    
        
    // MAC
    goby::acomms::MACManager mac_;    
    
    boost::asio::io_service timer_io_service_;
    boost::asio::io_service::work work_;

    goby::acomms::RouteManager* router_;
    
    // for PBDriver, IridiumDriver
    std::vector<boost::shared_ptr<goby::common::ZeroMQService> > zeromq_service_;

    // for UDPDriver
    std::vector<boost::shared_ptr<boost::asio::io_service> > asio_service_;
    
    std::vector<boost::shared_ptr<Timer> > timers_;


    std::map<boost::shared_ptr<goby::acomms::ModemDriverBase>, double > driver_restart_time_;

    std::set<const google::protobuf::Descriptor*> dccl_frontseat_forward_;
    
    static pAcommsHandlerConfig cfg_;
    static CpAcommsHandler* inst_;    
};

#endif 
