// Copyright 2017-2022:
//   GobySoft, LLC (2013-)
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

#ifndef GOBY_MOOS_MIDDLEWARE_MOOS_PLUGIN_TRANSLATOR_H
#define GOBY_MOOS_MIDDLEWARE_MOOS_PLUGIN_TRANSLATOR_H

#include <functional> // for function
#include <map>        // for map
#include <ostream>    // for operator<<
#include <set>        // for set
#include <string>     // for string, basic...
#include <thread>     // for get_id, opera...
#include <utility>    // for pair, make_pair

#include <MOOS/libMOOS/Comms/MOOSMsg.h> // for CMOOSMsg
#include <MOOS/libMOOS/DB/MsgFilter.h>
#include <boost/units/quantity.hpp>             // for operator*
#include <boost/units/systems/si/frequency.hpp> // for frequency, hertz

#include "MOOS/libMOOS/Comms/MOOSAsyncCommClient.h"    // for MOOSAsyncComm...
#include "goby/middleware/application/multi_thread.h"  // for SimpleThread
#include "goby/moos/protobuf/moos_gateway_config.pb.h" // for GobyMOOSGatew...
#include "goby/time/system_clock.h"                    // for SystemClock

class CMOOSCommClient;

namespace goby
{
namespace moos
{
bool TranslatorOnConnectCallBack(void* TranslatorBase);

class TranslatorBase
{
  public:
    TranslatorBase(const goby::apps::moos::protobuf::GobyMOOSGatewayConfig& config);

  protected:
    std::string translator_name()
    {
        std::stringstream ss;
        ss << std::this_thread::get_id();
        return std::string("goby::moos::Translator::" + ss.str());
    }

    class MOOSInterface
    {
      public:
        // MOOS
        void add_trigger(const std::string& moos_var,
                         const std::function<void(const CMOOSMsg&)>& func)
        {
            trigger_vars_.insert(std::make_pair(moos_var, func));
            if (connected_)
                moos_register(moos_var);
        }

        void add_wildcard_trigger(const std::string& moos_var_wildcard,
                                  const std::string& moos_app_wildcard,
                                  const std::function<void(const CMOOSMsg&)>& func)
        {
            MOOS::MsgFilter moos_filter(moos_app_wildcard, moos_var_wildcard);
            trigger_wildcard_vars_.insert(std::make_pair(moos_filter, func));
            if (connected_)
                moos_wildcard_register(moos_filter);
        }

        void add_buffer(const std::string& moos_var)
        {
            buffer_vars_.insert(moos_var);
            if (connected_)
                moos_register(moos_var);
        }
        std::map<std::string, CMOOSMsg>& buffer() { return buffer_; }
        CMOOSCommClient& comms() { return comms_; }
        void loop();

      private:
        friend bool TranslatorOnConnectCallBack(void* TranslatorBase);
        void on_connect();

        void moos_register(const std::string& moos_var)
        {
            comms_.Register(moos_var);
            goby::glog.is_debug1() && goby::glog << "Subscribed for MOOS variable: " << moos_var
                                                 << std::endl;
        }
        void moos_wildcard_register(const MOOS::MsgFilter& moos_filter)
        {
            comms_.Register(moos_filter.var_filter(), moos_filter.app_filter(), 0);
            goby::glog.is_debug1() &&
                goby::glog << "Subscribed for MOOS wildcard: variable: " << moos_filter.var_filter()
                           << ", app: " << moos_filter.app_filter() << std::endl;
        }

      private:
        std::map<std::string, std::function<void(const CMOOSMsg&)>> trigger_vars_;
        std::map<MOOS::MsgFilter, std::function<void(const CMOOSMsg&)>> trigger_wildcard_vars_;
        std::set<std::string> buffer_vars_;
        std::map<std::string, CMOOSMsg> buffer_;
        MOOS::MOOSAsyncCommClient comms_;
        goby::time::SystemClock::time_point next_time_publish_{goby::time::SystemClock::now()};
        bool connected_{false};
    };

    friend bool TranslatorOnConnectCallBack(void* TranslatorBase);
    MOOSInterface& moos() { return moos_; }
    void loop();

  private:
    MOOSInterface moos_;
    const goby::apps::moos::protobuf::GobyMOOSGatewayConfig cfg_;
};

template <template <class> class ThreadType>
class BasicTranslator : public TranslatorBase,
                        public ThreadType<goby::apps::moos::protobuf::GobyMOOSGatewayConfig>
{
  public:
    BasicTranslator(const goby::apps::moos::protobuf::GobyMOOSGatewayConfig& config)
        : TranslatorBase(config),
          ThreadType<goby::apps::moos::protobuf::GobyMOOSGatewayConfig>(
              config, 10 * boost::units::si::hertz) // config.poll_frequency()))
    {
    }

  protected:
    // Goby
    ThreadType<goby::apps::moos::protobuf::GobyMOOSGatewayConfig>& goby() { return *this; }

  private:
    void loop() override { this->TranslatorBase::loop(); }
};

using Translator = BasicTranslator<goby::middleware::SimpleThread>;

} // namespace moos
} // namespace goby

#endif
