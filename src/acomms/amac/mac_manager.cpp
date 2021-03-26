// Copyright 2010-2021:
//   GobySoft, LLC (2013-)
//   Massachusetts Institute of Technology (2007-2014)
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

#include <boost/date_time/posix_time/posix_time_duration.hpp> // for seconds
#include <boost/date_time/posix_time/ptime.hpp>               // for ptime
#include <cstdlib>                                            // for abs
#include <iostream>                                           // for basic_...

#include "goby/acomms/acomms_constants.h"               // for BROADC...
#include "goby/time/convert.h"                          // for convert
#include "goby/time/io.h"                               // for operat...
#include "goby/time/types.h"                            // for MicroTime
#include "goby/util/as.h"                               // for as
#include "goby/util/debug_logger/flex_ostream.h"        // for operat...
#include "goby/util/debug_logger/flex_ostreambuf.h"     // for DEBUG1
#include "goby/util/debug_logger/logger_manipulators.h" // for operat...
#include "goby/util/debug_logger/term_color.h"          // for green
#include "goby/util/protobuf/io.h"                      // for operat...
#include "mac_manager.h"

using goby::glog;
using namespace goby::util::logger;
using namespace goby::util::tcolor;

int goby::acomms::MACManager::count_;

goby::acomms::MACManager::MACManager()
    : current_slot_(std::list<protobuf::ModemTransmission>::begin())
{
    ++count_;

    glog_mac_group_ = "goby::acomms::amac::" + goby::util::as<std::string>(count_);
    goby::glog.add_group(glog_mac_group_, util::Colors::blue);
}

goby::acomms::MACManager::~MACManager() = default;

void goby::acomms::MACManager::startup(const protobuf::MACConfig& cfg)
{
    cfg_ = cfg;

    switch (cfg_.type())
    {
        case protobuf::MAC_POLLED:
        case protobuf::MAC_FIXED_DECENTRALIZED:
            std::list<protobuf::ModemTransmission>::clear();
            for (int i = 0, n = cfg_.slot_size(); i < n; ++i)
            {
                protobuf::ModemTransmission slot = cfg_.slot(i);
                slot.set_slot_index(i);
                std::list<protobuf::ModemTransmission>::push_back(slot);
            }

            if (cfg_.type() == protobuf::MAC_POLLED)
                glog.is(DEBUG1) && glog << group(glog_mac_group_)
                                        << "Using the Centralized Polling MAC_POLLED scheme"
                                        << std::endl;
            else if (cfg_.type() == protobuf::MAC_FIXED_DECENTRALIZED)
                glog.is(DEBUG1) && glog << group(glog_mac_group_)
                                        << "Using the Decentralized MAC_FIXED_DECENTRALIZED scheme"
                                        << std::endl;
            break;

        default: return;
    }

    restart();
}

void goby::acomms::MACManager::do_work()
{
    if (running() && goby::time::SystemClock::now() > next_slot_t_)
        begin_slot();
}

void goby::acomms::MACManager::restart()
{
    glog.is(DEBUG1) && glog << group(glog_mac_group_)
                            << "Goby Acoustic Medium Access Control module starting up."
                            << std::endl;

    if (started_up_)
    {
        glog.is(DEBUG1) && glog << group(glog_mac_group_)
                                << " ... MAC is already started, not restarting." << std::endl;
        return;
    }

    started_up_ = true;

    update();

    glog.is(DEBUG1) && glog << group(glog_mac_group_)
                            << "the first MAC TDMA cycle begins at time: " << next_slot_t_
                            << std::endl;
}

void goby::acomms::MACManager::shutdown()
{
    current_slot_ = std::list<protobuf::ModemTransmission>::begin();
    started_up_ = false;

    glog.is(DEBUG1) && glog << group(glog_mac_group_)
                            << "the MAC cycle has been shutdown until restarted." << std::endl;
}

void goby::acomms::MACManager::begin_slot()
{
    // check skew
    if (std::abs((time::SystemClock::now() - next_slot_t_) / std::chrono::microseconds(1)) >
        allowed_skew_ / std::chrono::microseconds(1))
    {
        glog.is(DEBUG1) && glog << group(glog_mac_group_) << warn
                                << "Clock skew detected, updating MAC." << std::endl;
        update();
        return;
    }

    protobuf::ModemTransmission s = *current_slot_;
    s.set_time_with_units(time::convert<time::MicroTime>(next_slot_t_));

    bool we_are_transmitting = true;
    switch (cfg_.type())
    {
        case protobuf::MAC_FIXED_DECENTRALIZED:
            // we only transmit if the packet source is us
            we_are_transmitting = (s.src() == cfg_.modem_id()) || s.always_initiate();
            break;

        case protobuf::MAC_POLLED:
            // we always transmit (poll)
            // but be quiet in the case where src = 0
            we_are_transmitting = (s.src() != BROADCAST_ID);
            break;

        default: break;
    }

    if (glog.is(DEBUG1))
    {
        glog << group(glog_mac_group_) << "Cycle order: [";

        for (auto it = std::list<protobuf::ModemTransmission>::begin(), n = end(); it != n; ++it)
        {
            if (it == current_slot_)
                glog << group(glog_mac_group_) << " " << green;

            switch (it->type())
            {
                case protobuf::ModemTransmission::DATA: glog << "d"; break;
                case protobuf::ModemTransmission::DRIVER_SPECIFIC: glog << "s"; break;

                default: break;
            }

            glog << it->src() << "/" << it->dest() << "@" << it->rate() << " " << nocolor;
        }
        glog << " ]" << std::endl;
    }

    glog.is(DEBUG1) && glog << group(glog_mac_group_) << "Starting slot: " << s.ShortDebugString()
                            << std::endl;

    if (we_are_transmitting)
        signal_initiate_transmission(s);
    signal_slot_start(s);

    increment_slot();

    glog.is(DEBUG1) && glog << group(glog_mac_group_) << "Next slot at " << next_slot_t_
                            << std::endl;
}

void goby::acomms::MACManager::increment_slot()
{
    switch (cfg_.type())
    {
        case protobuf::MAC_FIXED_DECENTRALIZED:
        case protobuf::MAC_POLLED:
            next_slot_t_ += time::convert_duration<std::chrono::microseconds>(
                current_slot_->slot_seconds_with_units());

            ++current_slot_;
            if (current_slot_ == std::list<protobuf::ModemTransmission>::end())
                current_slot_ = std::list<protobuf::ModemTransmission>::begin();
            break;

        default: break;
    }
}

goby::time::SystemClock::time_point goby::acomms::MACManager::next_cycle_time()
{
    auto now = time::SystemClock::now();

    decltype(now) reference;
    switch (cfg_.ref_time_type())
    {
        case protobuf::MACConfig::REFERENCE_START_OF_DAY:
            reference = time::convert<decltype(reference)>(
                boost::posix_time::ptime(time::convert<boost::posix_time::ptime>(now).date(),
                                         boost::posix_time::seconds(0)));
            break;
        case protobuf::MACConfig::REFERENCE_FIXED:
            reference = time::convert<decltype(reference)>(cfg_.fixed_ref_time_with_units());
            break;
    }

    time::SystemClock::duration duration_since_ref = now - reference;

    auto cycle_dur = cycle_duration();
    cycles_since_reference_ = (duration_since_ref / cycle_dur) + 1;

    glog.is(DEBUG2) && glog << group(glog_mac_group_) << "reference: " << reference << std::endl;

    glog.is(DEBUG2) &&
        glog << group(glog_mac_group_) << "duration since reference: "
             << std::chrono::duration_cast<std::chrono::microseconds>(duration_since_ref).count()
             << " us" << std::endl;

    glog.is(DEBUG2) &&
        glog << group(glog_mac_group_) << "cycle duration: "
             << std::chrono::duration_cast<std::chrono::microseconds>(cycle_dur).count() << " us"
             << std::endl;

    glog.is(DEBUG2) && glog << group(glog_mac_group_)
                            << "cycles since reference: " << cycles_since_reference_ << std::endl;

    auto time_to_next = cycles_since_reference_ * cycle_dur;

    return reference + time_to_next;
}

void goby::acomms::MACManager::update()
{
    glog.is(DEBUG1) && glog << group(glog_mac_group_) << "Updating MAC cycle." << std::endl;

    if (std::list<protobuf::ModemTransmission>::size() == 0)
    {
        glog.is(DEBUG1) && glog << group(glog_mac_group_)
                                << "the MAC TDMA cycle is empty. Stopping timer" << std::endl;
        started_up_ = false;
        return;
    }

    // reset the cycle to the beginning
    current_slot_ = std::list<protobuf::ModemTransmission>::begin();
    // advance the next slot time to the beginning of the next cycle
    next_slot_t_ = next_cycle_time();

    glog.is(DEBUG1) && glog << group(glog_mac_group_)
                            << "The next MAC TDMA cycle begins at time: " << next_slot_t_
                            << std::endl;

    // if we can start cycles in the middle, do it
    if (cfg_.start_cycle_in_middle() && std::list<protobuf::ModemTransmission>::size() > 1 &&
        (cfg_.type() == protobuf::MAC_FIXED_DECENTRALIZED || cfg_.type() == protobuf::MAC_POLLED))
    {
        glog.is(DEBUG1) && glog << group(glog_mac_group_)
                                << "Starting next available slot (in middle of cycle)" << std::endl;

        // step back a cycle
        next_slot_t_ -= cycle_duration();

        auto now = time::SystemClock::now();

        // skip slots until we're at a slot that is in the future
        while (next_slot_t_ < now) increment_slot();

        glog.is(DEBUG1) && glog << group(glog_mac_group_) << "Next slot at " << next_slot_t_
                                << std::endl;
    }
}

goby::time::SystemClock::duration goby::acomms::MACManager::cycle_duration()
{
    time::MicroTime length = 0;
    for (const protobuf::ModemTransmission& slot : *this)
        length += slot.slot_seconds_with_units<time::MicroTime>();
    return time::convert_duration<goby::time::SystemClock::duration>(length);
}

std::ostream& goby::acomms::operator<<(std::ostream& os, const MACManager& mac)
{
    for (const auto& it : mac) { os << it; }
    return os;
}
