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

#ifndef GOBY_APPS_MOOS_PTRANSLATOR_PTRANSLATOR_H
#define GOBY_APPS_MOOS_PTRANSLATOR_PTRANSLATOR_H

#include <memory> // for shared_ptr
#include <vector> // for vector

#include <boost/asio/basic_waitable_timer.hpp> // for basic_waitable_timer

#include "goby/moos/goby_moos_app.h"   // for GobyMOOSApp
#include "goby/moos/moos_translator.h" // for MOOSTranslator
#include "goby/time/system_clock.h"    // for SystemClock
#include "goby/util/asio_compat.h"

class CMOOSMsg;
namespace boost
{
namespace system
{
class error_code;
} // namespace system
} // namespace boost
namespace google
{
namespace protobuf
{
class Message;
} // namespace protobuf
} // namespace google

namespace goby
{
namespace moos
{
namespace protobuf
{
class TranslatorEntry;
} // namespace protobuf
} // namespace moos

namespace apps
{
namespace moos
{
namespace protobuf
{
class pTranslatorConfig;
} // namespace protobuf

class CpTranslator : public goby::moos::GobyMOOSApp
{
  public:
    static CpTranslator* get_instance();
    static void delete_instance();

  private:
    typedef boost::asio::basic_waitable_timer<goby::time::SystemClock> Timer;
    CpTranslator();
    ~CpTranslator() override;

    void loop() override; // from GobyMOOSApp

    void create_on_publish(const CMOOSMsg& trigger_msg,
                           const goby::moos::protobuf::TranslatorEntry& entry);
    void create_on_multiplex_publish(const CMOOSMsg& moos_msg);

    void create_on_timer(const boost::system::error_code& error,
                         const goby::moos::protobuf::TranslatorEntry& entry, Timer* timer);

    void do_translation(const goby::moos::protobuf::TranslatorEntry& entry);
    void do_publish(const std::shared_ptr<google::protobuf::Message>& created_message);

    void handle_lat_origin(const CMOOSMsg& msg);
    void handle_lon_origin(const CMOOSMsg& msg);

  private:
    enum
    {
        ALLOWED_TIMER_SKEW_SECONDS = 1
    };

    goby::moos::MOOSTranslator translator_;

    double lat_origin_;
    double lon_origin_;
    bool new_origin_;

    boost::asio::io_context timer_io_context_;
    boost::asio::io_context::work work_;

    std::vector<std::shared_ptr<Timer> > timers_;

    static protobuf::pTranslatorConfig cfg_;
    static CpTranslator* inst_;
};
} // namespace moos
} // namespace apps
} // namespace goby

#endif
