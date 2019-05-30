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

#ifndef BenthosATM900DriverFSM20161221H
#define BenthosATM900DriverFSM20161221H

#include <boost/circular_buffer.hpp>

#include <boost/format.hpp>
#include <boost/mpl/list.hpp>
#include <boost/statechart/custom_reaction.hpp>
#include <boost/statechart/deep_history.hpp>
#include <boost/statechart/event.hpp>
#include <boost/statechart/in_state_reaction.hpp>
#include <boost/statechart/simple_state.hpp>
#include <boost/statechart/state.hpp>
#include <boost/statechart/state_machine.hpp>
#include <boost/statechart/transition.hpp>
#include <iostream>

#include "goby/acomms/acomms_constants.h"
#include "goby/common/logger.h"
#include "goby/time.h"
#include "goby/util/binary.h"

#include "goby/acomms/protobuf/benthos_atm900.pb.h"
#include "goby/acomms/protobuf/modem_message.pb.h"

namespace goby
{
namespace acomms
{
namespace benthos_fsm
{
// events
struct EvRxSerial : boost::statechart::event<EvRxSerial>
{
    std::string line;
};
struct EvTxSerial : boost::statechart::event<EvTxSerial>
{
};

struct EvAck : boost::statechart::event<EvAck>
{
    EvAck(const std::string& response) : response_(response) {}

    std::string response_;
};

struct EvAtEmpty : boost::statechart::event<EvAtEmpty>
{
};
struct EvReset : boost::statechart::event<EvReset>
{
};

struct EvDial : boost::statechart::event<EvDial>
{
    EvDial(int dest, int rate) : dest_(dest), rate_(rate) {}

    int dest_;
    int rate_;
};

struct EvRange : boost::statechart::event<EvRange>
{
    EvRange(int dest) : dest_(dest) {}
    int dest_;
};

struct EvRequestLowPower : boost::statechart::event<EvRequestLowPower>
{
};
struct EvLowPower : boost::statechart::event<EvLowPower>
{
};

struct EvConnect : boost::statechart::event<EvConnect>
{
};
struct EvNoCarrier : boost::statechart::event<EvNoCarrier>
{
};

struct EvTransmit : boost::statechart::event<EvTransmit>
{
};

struct EvTransmitBegun : boost::statechart::event<EvTransmitBegun>
{
};

struct EvReceive : boost::statechart::event<EvReceive>
{
    EvReceive(const std::string& first) : first_(first) {}
    std::string first_;
};

struct EvReceiveComplete : boost::statechart::event<EvReceiveComplete>
{
};
struct EvShellPrompt : boost::statechart::event<EvShellPrompt>
{
};
struct EvRangingComplete : boost::statechart::event<EvRangingComplete>
{
};

struct Active;
struct ReceiveData;
struct Command;
struct Ready;
struct Configure;
struct SetClock;
struct Dial;
struct LowPower;
struct Range;

struct Online;
struct Listen;
struct TransmitData;

// state machine
struct BenthosATM900FSM : boost::statechart::state_machine<BenthosATM900FSM, Active>
{
  public:
    BenthosATM900FSM(const protobuf::DriverConfig& driver_cfg)
        : serial_tx_buffer_(SERIAL_BUFFER_CAPACITY), received_(RECEIVED_BUFFER_CAPACITY),
          driver_cfg_(driver_cfg), data_out_(DATA_BUFFER_CAPACITY)
    {
        ++count_;
        glog_fsm_group_ = "benthosatm900::fsm::" + goby::util::as<std::string>(count_);
    }

    void buffer_data_out(const goby::acomms::protobuf::ModemTransmission& msg);

    // messages for the serial line at next opportunity
    boost::circular_buffer<std::string>& serial_tx_buffer() { return serial_tx_buffer_; }

    // received messages to be passed up out of the ModemDriver
    boost::circular_buffer<protobuf::ModemTransmission>& received() { return received_; }

    // data that should (eventually) be sent out across the connection
    boost::circular_buffer<protobuf::ModemTransmission>& data_out() { return data_out_; }

    const protobuf::DriverConfig& driver_cfg() const { return driver_cfg_; }

    const std::string& glog_fsm_group() const { return glog_fsm_group_; }

  private:
    enum
    {
        SERIAL_BUFFER_CAPACITY = 10
    };
    enum
    {
        RECEIVED_BUFFER_CAPACITY = 10
    };

    boost::circular_buffer<std::string> serial_tx_buffer_;
    boost::circular_buffer<protobuf::ModemTransmission> received_;
    const protobuf::DriverConfig& driver_cfg_;

    enum
    {
        DATA_BUFFER_CAPACITY = 5
    };
    boost::circular_buffer<protobuf::ModemTransmission> data_out_;

    std::string glog_fsm_group_;

    static int count_;
};

struct StateNotify
{
    StateNotify(const std::string& name) : name_(name)
    {
        glog.is(goby::common::logger::DEBUG1) && glog << group("benthosatm900::fsm") << name_
                                                      << std::endl;
    }
    ~StateNotify()
    {
        glog.is(goby::common::logger::DEBUG1) && glog << group("benthosatm900::fsm") << "~" << name_
                                                      << std::endl;
    }

  private:
    std::string name_;
};

// Active
struct Active : boost::statechart::simple_state<Active, BenthosATM900FSM, Command,
                                                boost::statechart::has_deep_history>,
                StateNotify
{
    Active() : StateNotify("Active") {}
    ~Active() {}

    void in_state_react(const EvRxSerial&);

    typedef boost::mpl::list<
        boost::statechart::transition<EvReset, Active>,
        boost::statechart::in_state_reaction<EvRxSerial, Active, &Active::in_state_react>,
        boost::statechart::transition<EvReceive, ReceiveData> >
        reactions;
};

struct ReceiveData : boost::statechart::state<ReceiveData, BenthosATM900FSM>, StateNotify
{
    ReceiveData(my_context ctx);
    ~ReceiveData() {}

    void in_state_react(const EvRxSerial&);

    typedef boost::mpl::list<
        boost::statechart::in_state_reaction<EvRxSerial, ReceiveData, &ReceiveData::in_state_react>,
        boost::statechart::transition<EvReceiveComplete,
                                      boost::statechart::deep_history<Command> > >
        reactions;

    protobuf::ModemTransmission rx_msg_;
    unsigned reported_size_;
    std::string encoded_bytes_; // still base 255 encoded
};

// Command
struct Command : boost::statechart::simple_state<Command, Active, Configure,
                                                 boost::statechart::has_deep_history>,
                 StateNotify
{
  public:
    void in_state_react(const EvAck&);
    void in_state_react(const EvTxSerial&);
    boost::statechart::result react(const EvConnect&)
    {
        if (at_out_.empty() || at_out_.front().second != "+++")
        {
            at_out_.clear();
            return transit<Online>();
        }
        else
        {
            // connect when trying to send "+++"
            return discard_event();
        }
    }

    Command() : StateNotify("Command"), at_out_(AT_BUFFER_CAPACITY)
    {
        // in case we start up in Online mode - likely as the @OpMode=1 is the default
        context<Command>().push_at_command("+++");
        // the modem seems to like to reset the OpMode
        context<Command>().push_clam_command("@OpMode=0");
    }
    ~Command() {}

    typedef boost::mpl::list<
        boost::statechart::custom_reaction<EvConnect>,
        boost::statechart::in_state_reaction<EvAck, Command, &Command::in_state_react>,
        boost::statechart::in_state_reaction<EvTxSerial, Command, &Command::in_state_react> >
        reactions;

    struct ATSentenceMeta
    {
        ATSentenceMeta() : last_send_time_(0), tries_(0) {}
        double last_send_time_;
        int tries_;
    };

    void push_at_command(std::string cmd)
    {
        if (cmd != "+++")
            cmd = "AT" + cmd;

        at_out_.push_back(std::make_pair(ATSentenceMeta(), cmd));
    }
    void push_clam_command(const std::string& cmd)
    {
        at_out_.push_back(std::make_pair(ATSentenceMeta(), cmd));
    }

    boost::circular_buffer<std::pair<ATSentenceMeta, std::string> >& at_out() { return at_out_; }

  private:
    enum
    {
        AT_BUFFER_CAPACITY = 100
    };
    boost::circular_buffer<std::pair<ATSentenceMeta, std::string> > at_out_;
    enum
    {
        COMMAND_TIMEOUT_SECONDS = 2
    };

    enum
    {
        RETRIES_BEFORE_RESET = 10
    };
};

struct Configure : boost::statechart::state<Configure, Command>, StateNotify
{
    typedef boost::mpl::list<boost::statechart::transition<EvAtEmpty, SetClock> > reactions;

    Configure(my_context ctx) : my_base(ctx), StateNotify("Configure")
    {
        context<Command>().push_at_command("");

        // disable local echo to avoid confusing our parser
        context<Command>().push_clam_command("@P1EchoChar=Dis");

        if (context<BenthosATM900FSM>().driver_cfg().GetExtension(
                benthos::protobuf::BenthosATM900DriverConfig::factory_reset))
            context<Command>().push_clam_command("factory_reset");

        if (context<BenthosATM900FSM>().driver_cfg().HasExtension(
                benthos::protobuf::BenthosATM900DriverConfig::config_load))
        {
            context<Command>().push_clam_command(
                "cfg load " + context<BenthosATM900FSM>().driver_cfg().GetExtension(
                                  benthos::protobuf::BenthosATM900DriverConfig::config_load));
        }

        for (int i = 0, n = context<BenthosATM900FSM>().driver_cfg().ExtensionSize(
                            benthos::protobuf::BenthosATM900DriverConfig::config);
             i < n; ++i)
        {
            context<Command>().push_clam_command(
                context<BenthosATM900FSM>().driver_cfg().GetExtension(
                    benthos::protobuf::BenthosATM900DriverConfig::config, i));
        }

        // ensure serial output is the format we expect
        context<Command>().push_clam_command("@Prompt=7");
        context<Command>().push_clam_command("@Verbose=3");

        // Goby will handle retries
        context<Command>().push_clam_command("@DataRetry=0");

        // Send the data immediately after we post it
        context<Command>().push_clam_command("@FwdDelay=0.05");
        context<Command>().push_clam_command(
            "@LocalAddr=" +
            goby::util::as<std::string>(context<BenthosATM900FSM>().driver_cfg().modem_id()));

        // Hex format for data
        context<Command>().push_clam_command("@PrintHex=Ena");

        // Wake tones are required so the modem will resume from low power at packet receipt
        context<Command>().push_clam_command("@WakeTones=Ena");

        // Receive all packets, let Goby deal with discarding them
        context<Command>().push_clam_command("@RcvAll=Ena");

        // Show data for bad packets so we can stats
        context<Command>().push_clam_command("@ShowBadData=Ena");

        // start up in Command mode after reboot/lowpower resume
        context<Command>().push_clam_command("@OpMode=0");

        // store the current configuration for later inspection
        // context<Command>().push_clam_command("cfg store /ffs/goby.ini");
    }

    ~Configure() {}
};

struct SetClock : boost::statechart::state<SetClock, Command>, StateNotify
{
    typedef boost::mpl::list<boost::statechart::transition<EvAtEmpty, Ready> > reactions;

    SetClock(my_context ctx) : my_base(ctx), StateNotify("SetClock")
    {
        auto p = time::SystemClock::now<boost::posix_time::ptime>();

        std::string date_str = boost::str(boost::format("-d%02d/%02d/%04d") %
                                          (int)p.date().month() % p.date().day() % p.date().year());
        std::string time_str =
            boost::str(boost::format("-t%02d:%02d:%02d") % p.time_of_day().hours() %
                       p.time_of_day().minutes() % p.time_of_day().seconds());

        context<Command>().push_clam_command("date " + time_str + " " + date_str);
    }

    ~SetClock() {}
};

struct Ready : boost::statechart::simple_state<Ready, Command>, StateNotify
{
  private:
    void in_state_react(const EvRequestLowPower&) { context<Command>().push_at_command("L"); }

  public:
    Ready() : StateNotify("Ready") {}
    ~Ready() {}

    typedef boost::mpl::list<
        boost::statechart::transition<EvDial, Dial>, boost::statechart::transition<EvRange, Range>,
        boost::statechart::in_state_reaction<EvRequestLowPower, Ready, &Ready::in_state_react>,
        boost::statechart::transition<EvLowPower, LowPower> >
        reactions;
};

struct Dial : boost::statechart::state<Dial, Command>, StateNotify
{
    enum
    {
        BENTHOS_BROADCAST_ID = 255
    };
    enum
    {
        DEFAULT_RATE = 2,
        RATE_MIN = 2,
        RATE_MAX = 13
    };

    Dial(my_context ctx)
        : my_base(ctx), StateNotify("Dial"), dest_(BENTHOS_BROADCAST_ID), rate_(DEFAULT_RATE)
    {
        if (const EvDial* evdial = dynamic_cast<const EvDial*>(triggering_event()))
        {
            dest_ = evdial->dest_;
            if (dest_ == goby::acomms::BROADCAST_ID)
                dest_ = BENTHOS_BROADCAST_ID;

            if (evdial->rate_ >= RATE_MIN && evdial->rate_ <= RATE_MAX)
                rate_ = evdial->rate_;
        }
        context<Command>().push_clam_command("@RemoteAddr=" + goby::util::as<std::string>(dest_));
        context<Command>().push_clam_command("@TxRate=" + goby::util::as<std::string>(rate_));
        context<Command>().push_at_command("O");
    }
    ~Dial() {}

  private:
    int dest_;
    int rate_;
};

struct LowPower : boost::statechart::state<LowPower, Command>, StateNotify
{
    LowPower(my_context ctx) : my_base(ctx), StateNotify("LowPower") {}
    ~LowPower() {}
};

struct Range : boost::statechart::state<Range, Command>, StateNotify
{
    void in_state_react(const EvRxSerial&);

    Range(my_context ctx) : my_base(ctx), StateNotify("Range"), dest_(0)
    {
        if (const EvRange* ev = dynamic_cast<const EvRange*>(triggering_event()))
        {
            dest_ = ev->dest_;
        }
        context<Command>().push_at_command("R" + goby::util::as<std::string>(dest_));
    }
    ~Range() {}

    typedef boost::mpl::list<
        boost::statechart::transition<EvRangingComplete, Ready>,
        boost::statechart::in_state_reaction<EvRxSerial, Range, &Range::in_state_react> >
        reactions;

  private:
    int dest_;
};

// Online
struct Online : boost::statechart::state<Online, Active, Listen>, StateNotify
{
    Online(my_context ctx) : my_base(ctx), StateNotify("Online") {}
    ~Online() {}

    typedef boost::mpl::list<
        boost::statechart::transition<EvShellPrompt, boost::statechart::deep_history<Command> > >
        reactions;
};

struct Listen : boost::statechart::state<Listen, Online>, StateNotify
{
    Listen(my_context ctx) : my_base(ctx), StateNotify("Listen")
    {
        if (!context<BenthosATM900FSM>().data_out().empty())
            post_event(EvTransmit());
    }
    ~Listen() {}

    typedef boost::mpl::list<boost::statechart::transition<EvTransmit, TransmitData> > reactions;
};

struct TransmitData : boost::statechart::state<TransmitData, Online>, StateNotify
{
    TransmitData(my_context ctx) : my_base(ctx), StateNotify("TransmitData") {}
    ~TransmitData() {}

    void in_state_react(const EvTxSerial&);
    void in_state_react(const EvAck&);

    typedef boost::mpl::list<
        boost::statechart::transition<EvTransmitBegun, Ready>,
        boost::statechart::in_state_reaction<EvTxSerial, TransmitData,
                                             &TransmitData::in_state_react>,
        boost::statechart::in_state_reaction<EvAck, TransmitData, &TransmitData::in_state_react> >
        reactions;
};

} // namespace benthos_fsm
} // namespace acomms
} // namespace goby

#endif
