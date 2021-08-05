// Copyright 2011-2021:
//   GobySoft, LLC (2013-)
//   Massachusetts Institute of Technology (2007-2014)
//   Community contributors (see AUTHORS file)
// File authors:
//   Toby Schneider <toby@gobysoft.org>
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

#ifndef GOBY_APPS_MOOS_PACOMMSHANDLER_PACOMMSHANDLER_H
#define GOBY_APPS_MOOS_PACOMMSHANDLER_PACOMMSHANDLER_H

#include <map>    // for map
#include <memory> // for shared_ptr
#include <set>    // for set
#include <string> // for string
#include <vector> // for vector

#include <boost/asio/basic_waitable_timer.hpp> // for basic_waita...
#include <boost/smart_ptr/shared_ptr.hpp>      // for shared_ptr

#include "goby/acomms/amac/mac_manager.h"                // for MACManager
#include "goby/acomms/modemdriver/driver_base.h"         // for ModemDriver...
#include "goby/acomms/queue/queue_manager.h"             // for QueueManager
#include "goby/moos/goby_moos_app.h"                     // for GobyMOOSApp
#include "goby/moos/moos_translator.h"                   // for MOOSTranslator
#include "goby/moos/protobuf/pAcommsHandler_config.pb.h" // for pAcommsHand...
#include "goby/time/system_clock.h"                      // for SystemClock
#include "goby/util/asio_compat.h"

class CMOOSMsg;
namespace goby
{
namespace acomms
{
class DCCLCodec;
class ModemDriverException;
class RouteManager;
namespace protobuf
{
class DriverConfig;
class ModemRaw;
class ModemTransmission;
} // namespace protobuf
} // namespace acomms
namespace moos
{
namespace protobuf
{
class TranslatorEntry;
} // namespace protobuf
} // namespace moos
} // namespace goby
namespace google
{
namespace protobuf
{
class Descriptor;
class Message;
} // namespace protobuf
} // namespace google

namespace boost
{
namespace system
{
class error_code;
} // namespace system

inline bool operator<(const shared_ptr<goby::acomms::ModemDriverBase>& lhs,
                      const shared_ptr<goby::acomms::ModemDriverBase>& rhs)
{
    int lhs_count = lhs ? lhs->driver_order() : 0;
    int rhs_count = rhs ? rhs->driver_order() : 0;

    return lhs_count < rhs_count;
}
} // namespace boost

namespace goby
{
namespace apps
{
namespace moos
{
class CpAcommsHandler : public goby::moos::GobyMOOSApp
{
  public:
    static CpAcommsHandler* get_instance();
    static void delete_instance();
    static std::map<std::string, void*> driver_plugins_;

  private:
    typedef boost::asio::basic_waitable_timer<goby::time::SystemClock> Timer;

    CpAcommsHandler();
    ~CpAcommsHandler() override;
    void loop() override; // from GobyMOOSApp

    void process_configuration();
    void create_driver(std::shared_ptr<goby::acomms::ModemDriverBase>& driver,
                       goby::acomms::protobuf::DriverConfig* driver_cfg,
                       goby::acomms::MACManager* mac);

    void create_on_publish(const CMOOSMsg& trigger_msg,
                           const goby::moos::protobuf::TranslatorEntry& entry);
    void create_on_multiplex_publish(const CMOOSMsg& moos_msg);
    void create_on_timer(const boost::system::error_code& error,
                         const goby::moos::protobuf::TranslatorEntry& entry, Timer* timer);

    void translate_and_push(const goby::moos::protobuf::TranslatorEntry& entry);

    // from QueueManager
    void handle_queue_receive(const google::protobuf::Message& msg);

    void handle_goby_signal(const google::protobuf::Message& msg1, const std::string& moos_var1,
                            const google::protobuf::Message& msg2, const std::string& moos_var2);

    void handle_raw(const goby::acomms::protobuf::ModemRaw& msg, const std::string& moos_var);

    void handle_mac_cycle_update(const CMOOSMsg& msg);
    void handle_flush_queue(const CMOOSMsg& msg);
    void handle_external_initiate_transmission(const CMOOSMsg& msg);
    void handle_external_driver_receive(const CMOOSMsg& msg);

    void handle_config_file_request(const CMOOSMsg& msg);

    void handle_driver_reset(const CMOOSMsg& msg);

    void handle_driver_cfg_update(const goby::acomms::protobuf::DriverConfig& cfg);

    void handle_encode_on_demand(const goby::acomms::protobuf::ModemTransmission& request_msg,
                                 google::protobuf::Message* data_msg);
    void handle_lat_origin(const CMOOSMsg& msg);
    void handle_lon_origin(const CMOOSMsg& msg);

    void driver_bind();
    void driver_unbind();
    void
    driver_reset(const std::shared_ptr<goby::acomms::ModemDriverBase>& driver,
                 const goby::acomms::ModemDriverException& e,
                 protobuf::pAcommsHandlerConfig::DriverFailureApproach::DriverFailureTechnique =
                     cfg_.driver_failure_approach().technique());

    void restart_drivers();

    enum
    {
        ALLOWED_TIMER_SKEW_SECONDS = 1
    };

  private:
    goby::moos::MOOSTranslator translator_;

  double lat_origin_;
  double lon_origin_;
  bool new_origin_;
  
     // new DCCL2 codec
    goby::acomms::DCCLCodec* dccl_;

    // manages queues and does additional packing
    goby::acomms::QueueManager queue_manager_;

    // driver class that interfaces to the modem
    std::shared_ptr<goby::acomms::ModemDriverBase> driver_;

    // driver and additional listener drivers (receive only)
    std::map<std::shared_ptr<goby::acomms::ModemDriverBase>, goby::acomms::protobuf::DriverConfig*>
        drivers_;

    // MAC
    goby::acomms::MACManager mac_;

    boost::asio::io_context timer_io_context_;
    boost::asio::io_context::work work_;

    goby::acomms::RouteManager* router_{nullptr};

    std::vector<std::shared_ptr<Timer>> timers_;

    std::map<std::shared_ptr<goby::acomms::ModemDriverBase>, double> driver_restart_time_;

    std::set<const google::protobuf::Descriptor*> dccl_frontseat_forward_;

    static protobuf::pAcommsHandlerConfig cfg_;
    static CpAcommsHandler* inst_;
};
} // namespace moos
} // namespace apps
} // namespace goby

#endif
