// Copyright 2017-2020:
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

#ifndef MOOS_PLUGIN_TRANSLATOR_20171020H
#define MOOS_PLUGIN_TRANSLATOR_20171020H

#include "MOOS/libMOOS/Comms/MOOSAsyncCommClient.h"
#include "goby/middleware/application/multi_thread.h"
#include "goby/middleware/transport/interthread.h"
#include "goby/moos/protobuf/moos_gateway_config.pb.h"
#include "goby/zeromq/application/multi_thread.h"
#include "goby/zeromq/transport/interprocess.h"

namespace goby
{
namespace moos
{
bool TranslatorOnConnectCallBack(void* Translator);

class Translator
    : public goby::middleware::SimpleThread<goby::apps::moos::protobuf::GobyMOOSGatewayConfig>
{
  public:
    Translator(const goby::apps::moos::protobuf::GobyMOOSGatewayConfig& config);

  protected:
    std::string translator_name()
    {
        std::stringstream ss;
        ss << std::this_thread::get_id();
        return std::string("goby::moos::Translator::" + ss.str());
    }

    // Goby
    goby::middleware::SimpleThread<goby::apps::moos::protobuf::GobyMOOSGatewayConfig>& goby()
    {
        return *this;
    }

    class MOOSInterface
    {
      public:
        // MOOS
        void add_trigger(const std::string& moos_var, std::function<void(const CMOOSMsg&)> func)
        {
            trigger_vars_.insert(std::make_pair(moos_var, func));
        }
        void add_buffer(const std::string& moos_var) { buffer_vars_.insert(moos_var); }
        std::map<std::string, CMOOSMsg>& buffer() { return buffer_; }
        CMOOSCommClient& comms() { return comms_; }
        void loop();

      private:
        friend bool TranslatorOnConnectCallBack(void* Translator);
        void on_connect();

      private:
        std::map<std::string, std::function<void(const CMOOSMsg&)>> trigger_vars_;
        std::set<std::string> buffer_vars_;
        std::map<std::string, CMOOSMsg> buffer_;
        MOOS::MOOSAsyncCommClient comms_;
        goby::time::SystemClock::time_point next_time_publish_{goby::time::SystemClock::now()};
    };

    friend bool TranslatorOnConnectCallBack(void* Translator);
    MOOSInterface& moos() { return moos_; }

  private:
    void loop() override;

  private:
    MOOSInterface moos_;
};

} // namespace moos
} // namespace goby

#endif
