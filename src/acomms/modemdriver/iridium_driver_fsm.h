// Copyright 2009-2017 Toby Schneider (http://gobysoft.org/index.wt/people/toby)
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

#ifndef IridiumModemDriverFSM20130826H
#define IridiumModemDriverFSM20130826H

#include <boost/circular_buffer.hpp>

#include <boost/statechart/event.hpp>
#include <boost/statechart/state_machine.hpp>
#include <boost/statechart/simple_state.hpp>
#include <boost/statechart/state.hpp>
#include <boost/statechart/transition.hpp>
#include <boost/statechart/in_state_reaction.hpp>
#include <boost/statechart/custom_reaction.hpp>
#include <boost/statechart/deep_history.hpp>
#include <boost/mpl/list.hpp>
#include <iostream>

#include "goby/acomms/protobuf/modem_message.pb.h"
#include "goby/acomms/protobuf/iridium_driver.pb.h"
#include "goby/acomms/modemdriver/iridium_driver_common.h"

namespace goby
{
    namespace acomms
    {

        inline unsigned sbd_csum(const std::string& data)
        {
            unsigned int csum = 0;
            for(std::string::const_iterator it = data.begin(), end = data.end(); it!=end; ++it)
                csum += *it & 0xFF;
            return csum;
        }
        
        
        namespace fsm
        {

            struct StateNotify 
            {
                StateNotify(const std::string& name)
                : name_(name)
                    {
                        glog.is(goby::common::logger::DEBUG1) && glog << group("iridiumdriver") << name_ << std::endl;
                    } 
                ~StateNotify()
                    {
                        glog.is(goby::common::logger::DEBUG1) && glog << group("iridiumdriver") << "~" << name_ << std::endl;
                    }
            private:
                std::string name_;
            };
            
            
            // events
            struct EvRxSerial : boost::statechart::event< EvRxSerial > { std::string line; };
            struct EvTxSerial : boost::statechart::event< EvTxSerial > { };

            struct EvRxOnCallSerial : boost::statechart::event< EvRxOnCallSerial > { std::string line; };
            struct EvTxOnCallSerial : boost::statechart::event< EvTxOnCallSerial > { };
            
            struct EvAck : boost::statechart::event< EvAck >
            {
                EvAck(const std::string& response)
                    : response_(response) { }
                
                std::string response_;
            };
            
            struct EvAtEmpty : boost::statechart::event< EvAtEmpty > {};
            struct EvReset : boost::statechart::event< EvReset > {};

            struct EvDial : boost::statechart::event< EvDial > {};
            struct EvRing : boost::statechart::event< EvRing > {};
            struct EvOnline : boost::statechart::event< EvOnline > {};

            struct EvHangup : boost::statechart::event< EvHangup > {};

            struct EvConnect : boost::statechart::event< EvConnect > {};
            struct EvNoCarrier : boost::statechart::event< EvNoCarrier > {};
            struct EvDisconnect : boost::statechart::event< EvDisconnect > {};


            struct EvSendBye : boost::statechart::event< EvSendBye > {};

            
            struct EvConfigured : boost::statechart::event< EvConfigured > {};
            struct EvSBDBeginData : boost::statechart::event< EvSBDBeginData >
            {
              EvSBDBeginData(const std::string& data = "", bool in_response_to_ring_alert = false)
                  : data_(data),
                    in_response_to_ring_alert_(in_response_to_ring_alert)
                    { }
                std::string data_;
                bool in_response_to_ring_alert_;
            };
            
            
            struct EvSBDSendBufferCleared : boost::statechart::event< EvSBDSendBufferCleared > {};

            struct EvSBDWriteReady : boost::statechart::event < EvSBDWriteReady > {};
            struct EvSBDWriteComplete : boost::statechart::event < EvSBDWriteComplete > {};
            struct EvSBDTransmitComplete : boost::statechart::event < EvSBDTransmitComplete >
            {
              EvSBDTransmitComplete(const std::string& sbdi)
                  : sbdi_(sbdi) { }
                std::string sbdi_;
            };
            struct EvSBDReceiveComplete : boost::statechart::event < EvSBDReceiveComplete > {};
            
            // pre-declare
            struct Active;
            struct Command;
            struct Configure;
            struct Ready;
            struct Answer;
            struct Dial;
            struct HangingUp;
            struct PostDisconnected;
            
            struct Online;
            struct OnCall;
            struct NotOnCall;

            struct SBD;
            struct SBDConfigure;
            struct SBDReady;
            struct SBDClearBuffers;
            struct SBDWrite;
            struct SBDTransmit;
            struct SBDReceive;
            
// state machine
            struct IridiumDriverFSM : boost::statechart::state_machine<IridiumDriverFSM, Active > 
            {
              public:
              IridiumDriverFSM(const protobuf::DriverConfig& driver_cfg)
                    : serial_tx_buffer_(SERIAL_BUFFER_CAPACITY),
                    received_(RECEIVED_BUFFER_CAPACITY),
                    driver_cfg_(driver_cfg),
                    data_out_(DATA_BUFFER_CAPACITY)
                {
                    ++count_;
                    glog_ir_group_ = "iridiumdriver::" + goby::util::as<std::string>(count_);
                }
                
                void buffer_data_out(const goby::acomms::protobuf::ModemTransmission& msg);
                
                // messages for the serial line at next opportunity
                boost::circular_buffer<std::string>& serial_tx_buffer() {return serial_tx_buffer_;}

                // received messages to be passed up out of the ModemDriver
                boost::circular_buffer<protobuf::ModemTransmission>& received() {return received_;}

                // data that should (eventually) be sent out across the Iridium connection
                boost::circular_buffer<protobuf::ModemTransmission>& data_out() {return data_out_;}
                
                const protobuf::DriverConfig& driver_cfg() const {return driver_cfg_;}

                const std::string& glog_ir_group() const { return glog_ir_group_; }                

                

              private:
                enum  { SERIAL_BUFFER_CAPACITY = 10 };
                enum  { RECEIVED_BUFFER_CAPACITY = 10 };
                
                boost::circular_buffer<std::string> serial_tx_buffer_;
                boost::circular_buffer<protobuf::ModemTransmission> received_;
                const protobuf::DriverConfig& driver_cfg_;

                enum  { DATA_BUFFER_CAPACITY = 5 };
                boost::circular_buffer<protobuf::ModemTransmission> data_out_;

                std::string glog_ir_group_;
                
                static int count_;
            };

            struct Active: boost::statechart::simple_state< Active, IridiumDriverFSM,
                boost::mpl::list<Command, NotOnCall> >
            {

                typedef boost::mpl::list<
                    boost::statechart::transition< EvReset, Active >
                    > reactions;
            };
            
            // Command
            struct Command : boost::statechart::simple_state<Command, Active::orthogonal<0>,
                boost::mpl::list<Configure, SBD> >,
                StateNotify
            {
              public:
                void in_state_react( const EvRxSerial& );
                void in_state_react( const EvTxSerial& );
                void in_state_react( const EvAck & );

                  Command()
                      : StateNotify("Command"),
                    at_out_(AT_BUFFER_CAPACITY)
                    { } 
                    ~Command() { }
                    
                    typedef boost::mpl::list<
                        boost::statechart::in_state_reaction< EvRxSerial, Command, &Command::in_state_react >,
                        boost::statechart::in_state_reaction< EvTxSerial, Command, &Command::in_state_react >,
                        boost::statechart::transition< EvOnline, Online >,
                        boost::statechart::in_state_reaction< EvAck, Command, &Command::in_state_react >
                        > reactions;


                    struct ATSentenceMeta
                    {
                    ATSentenceMeta() :
                        last_send_time_(0),
                            tries_(0)
                            { }
                        double last_send_time_;
                        int tries_;
                    };

                    void push_at_command(const std::string& cmd)
                    {
                        at_out_.push_back(std::make_pair(ATSentenceMeta(), cmd));
                    }
                
                    boost::circular_buffer< std::pair<ATSentenceMeta, std::string> >& at_out() {return at_out_;}
                
                    void clear_sbd_rx_buffer() { sbd_rx_buffer_.clear(); }

                    void handle_sbd_rx(const std::string& in);

                    
                    
                  private:
                    enum  { AT_BUFFER_CAPACITY = 100 };
                    boost::circular_buffer< std::pair<ATSentenceMeta, std::string> > at_out_;
                    enum { COMMAND_TIMEOUT_SECONDS = 2,
                           DIAL_TIMEOUT_SECONDS = 60,
                           SBDIX_TIMEOUT_SECONDS = DIAL_TIMEOUT_SECONDS,
                           TRIPLE_PLUS_TIMEOUT_SECONDS = 6,
                           HANGUP_TIMEOUT_SECONDS = 10,
                           ANSWER_TIMEOUT_SECONDS = 30};

                    enum { RETRIES_BEFORE_RESET = 3 };
                    std::string sbd_rx_buffer_;
                };

            struct Configure : boost::statechart::state<Configure, Command::orthogonal<0> >, StateNotify
            {    
                typedef boost::mpl::list<
                    boost::statechart::transition< EvAtEmpty, Ready >
                    > reactions;
                    
              Configure(my_context ctx) : my_base(ctx),
                    StateNotify("Configure")
                    {
                        context<Command>().push_at_command("");
                        for(int i = 0,
                                n = context<IridiumDriverFSM>().driver_cfg().ExtensionSize(
                                    IridiumDriverConfig::config);
                            i < n; ++i)
                        {
                            context<Command>().push_at_command(
                                context<IridiumDriverFSM>().driver_cfg().GetExtension(
                                    IridiumDriverConfig::config, i));
                        }
                    }

                ~Configure()
                {
                    post_event(EvConfigured());
                } 
            };


            struct Ready : boost::statechart::simple_state<Ready, Command::orthogonal<0> >, StateNotify
            {
              public:
              Ready() : StateNotify("Ready") { } 
                ~Ready() { }

                boost::statechart::result react( const EvDial& )
                {
                    if(state_downcast<const NotOnCall *>() != 0)
                    {
                        return transit<Dial>();
                    }
                    else
                    {
                        glog.is(goby::common::logger::DEBUG1) && glog << group("iridiumdriver") << "Not dialing since we are already on a call." << std::endl;
                        return discard_event();
                    }
                }
                
                typedef boost::mpl::list<
                    boost::statechart::transition< EvRing, Answer >,
                    boost::statechart::custom_reaction< EvDial >
                    > reactions;

              private:
            };


            struct HangingUp : boost::statechart::state<HangingUp, Command::orthogonal<0> >, StateNotify
            {
              public:
              HangingUp(my_context ctx) : my_base(ctx),
                    StateNotify("HangingUp")
                {
                    context<Command>().push_at_command("+++");
                    context<Command>().push_at_command("H");
                } 
                ~HangingUp() { }
                
                typedef boost::mpl::list<
                    boost::statechart::transition< EvAtEmpty, Ready >
                    > reactions;
                    
              private:
            };

            struct PostDisconnected : boost::statechart::state<PostDisconnected, Command::orthogonal<0> >, StateNotify
            {
              public:
              PostDisconnected(my_context ctx) : my_base(ctx),
                    StateNotify("PostDisconnected")
                {
                    glog.is(goby::common::logger::DEBUG1) && glog << group("iridiumdriver") << "Disconnected; checking error details: " << std::endl;
                    context<Command>().push_at_command("+CEER");
                } 
                ~PostDisconnected() { }
                
                typedef boost::mpl::list<
                    boost::statechart::transition< EvAtEmpty, Ready >
                    > reactions;
                    
              private:
            };

            
            struct Dial : boost::statechart::state<Dial, Command::orthogonal<0> >, StateNotify
            {
                typedef boost::mpl::list<
                    boost::statechart::custom_reaction< EvNoCarrier >
                    > reactions;
    
              Dial(my_context ctx) : my_base(ctx),
                    StateNotify("Dial"),
                    dial_attempts_(0)
                {
                    dial();
                } 
                ~Dial(){ }
                

                boost::statechart::result react( const EvNoCarrier& );
                void dial();
                
                
              private:
                int dial_attempts_;
            };

            struct Answer : boost::statechart::state<Answer, Command::orthogonal<0> >, StateNotify
            {
                typedef boost::mpl::list<
                    boost::statechart::transition< EvNoCarrier, Ready >
                    > reactions;

              Answer(my_context ctx) : my_base(ctx), StateNotify("Answer")
                {
                    context<Command>().push_at_command("A");
                }
                ~Answer() { } 
            };

// Online
            struct Online : boost::statechart::simple_state<Online, Active::orthogonal<0> >, StateNotify
            {
                
              Online() : StateNotify("Online") { }
                ~Online() { }
                
                void in_state_react( const EvRxSerial& );
                void in_state_react( const EvTxSerial& );

                typedef boost::mpl::list<
                    boost::statechart::transition< EvHangup, HangingUp >,
                    boost::statechart::transition< EvDisconnect, PostDisconnected >,
                    boost::statechart::in_state_reaction< EvRxSerial, Online, &Online::in_state_react >,
                    boost::statechart::in_state_reaction< EvTxSerial, Online, &Online::in_state_react >
                    > reactions;
                
                
            };

            // Orthogonal on-call / not-on-call
            struct NotOnCall : boost::statechart::simple_state<NotOnCall, Active::orthogonal<1> >, StateNotify
            {
                typedef boost::mpl::list<
                    boost::statechart::transition< EvConnect, OnCall >
                    > reactions;
                
              NotOnCall() : StateNotify("NotOnCall") {}
                ~NotOnCall() {} 
            };


            struct OnCall : boost::statechart::state<OnCall, Active::orthogonal<1> >, StateNotify, OnCallBase
            {
              public:

            OnCall(my_context ctx) : my_base(ctx), StateNotify("OnCall")
                {
                    // add a brief identifier that is *different* than the "~" which is what PPP uses
                    // add a carriage return to clear out any garbage
                    // at the *beginning* of transmission
                    context<IridiumDriverFSM>().serial_tx_buffer().push_front("goby\r");
                    
                    // connecting necessarily puts the DTE online
                    post_event(EvOnline());
                } 
                ~OnCall() {
                    // signal the disconnect event for the command state to handle
                    glog.is(goby::common::logger::DEBUG1) && glog << group("iridiumdriver") << "Sent " << total_bytes_sent() << " bytes on this call." << std::endl;
                    post_event(EvDisconnect());
                } 

                void in_state_react( const EvRxOnCallSerial& );
                void in_state_react( const EvTxOnCallSerial& );
                void in_state_react( const EvSendBye& );
                
                typedef boost::mpl::list<
                    boost::statechart::transition< EvNoCarrier, NotOnCall >,
                    boost::statechart::in_state_reaction< EvRxOnCallSerial, OnCall, &OnCall::in_state_react >,
                    boost::statechart::in_state_reaction< EvTxOnCallSerial, OnCall, &OnCall::in_state_react >,
                    boost::statechart::in_state_reaction< EvSendBye, OnCall, &OnCall::in_state_react >

                    > reactions;    

              private:

           
            };


            struct SBD : boost::statechart::simple_state<SBD, Command::orthogonal<1>, SBDReady >,
                StateNotify
                {
                  SBD() : StateNotify("SBD") {
                    }
                    ~SBD() { }

                    void set_data(const EvSBDBeginData& e)
                    {
                        set_data(e.data_);
                        in_response_to_ring_alert_ = e.in_response_to_ring_alert_;
                    }
                    void clear_data() { data_.clear(); }
                    const std::string& data() const { return data_; }
                    bool in_response_to_ring_alert() const { return in_response_to_ring_alert_; }
                    
                  private:
                    void set_data(const std::string& data)
                    {
                        if(data.empty())
                            data_ = data;
                        else
                        {
                            unsigned int csum = sbd_csum(data);
                            
                            const int bits_in_byte = 8;
                            data_ = data + std::string(1, (csum & 0xFF00) >> bits_in_byte) + std::string(1, (csum & 0xFF));
                        }
                    }

                  private:
                    std::string data_;
                    bool in_response_to_ring_alert_;
                };
            
            /* struct SBDConfigure : boost::statechart::simple_state<SBDConfigure, SBD >, StateNotify */
            /* { */
            /*     typedef boost::mpl::list< */
            /*         boost::statechart::transition< EvConfigured, SBDReady > */
            /*         > reactions; */
                
            /*   SBDConfigure() : StateNotify("SBDConfigure") */
            /*     { */
            /*     } */
            /*     ~SBDConfigure() { */
            /*     } */
                
            /* }; */
            
                
            struct SBDReady: boost::statechart::simple_state<SBDReady, SBD >, StateNotify
            {
              typedef boost::mpl::list<
                  boost::statechart::transition< EvSBDBeginData, SBDClearBuffers, SBD, &SBD::set_data >
                  > reactions;

              SBDReady() : StateNotify("SBDReady") {
              }
              
              ~SBDReady(){
              }            
            };

            struct SBDClearBuffers : boost::statechart::state<SBDClearBuffers, SBD >, StateNotify
            {
                typedef boost::mpl::list<
                    boost::statechart::transition< EvSBDSendBufferCleared, SBDWrite >
                    > reactions;
                                
              SBDClearBuffers(my_context ctx) : my_base(ctx), StateNotify("SBDClearBuffers")
                {
                    context<Command>().clear_sbd_rx_buffer();
                    context<Command>().push_at_command("+SBDD2");
                }
                
                ~SBDClearBuffers()
                {
                }
            };
            
            
            
            
            struct SBDWrite : boost::statechart::state<SBDWrite, SBD >, StateNotify
            {
 
              SBDWrite(my_context ctx) : my_base(ctx), StateNotify("SBDWrite")
                {
                    if(context<SBD>().data().empty())
                    {
                        glog.is(goby::common::logger::DEBUG1) && glog << group("iridiumdriver") << "Mailbox Check." << std::endl;
                        post_event(EvSBDWriteComplete()); // Mailbox Check
                    }
                    else
                    {
                        glog.is(goby::common::logger::DEBUG1) && glog << group("iridiumdriver") << "Writing data." << std::endl;

                        const int csum_bytes = 2;
                        context<Command>().push_at_command("+SBDWB=" + goby::util::as<std::string>(context<SBD>().data().size()-csum_bytes));
                    }
                    
                }
                
                void in_state_react( const EvSBDWriteReady& )
                {
                    context<IridiumDriverFSM>().serial_tx_buffer().push_back(context<SBD>().data());
                }
                

                ~SBDWrite() {
                }

               typedef boost::mpl::list<
                    boost::statechart::in_state_reaction< EvSBDWriteReady, SBDWrite, &SBDWrite::in_state_react >,
                    boost::statechart::transition< EvSBDWriteComplete, SBDTransmit >
                    > reactions;
                
                
                
            };




            struct SBDTransmit : boost::statechart::state<SBDTransmit, SBD >, StateNotify
            {
                typedef boost::mpl::list<
                    boost::statechart::custom_reaction< EvSBDTransmitComplete >
                    > reactions;
              SBDTransmit(my_context ctx) : my_base(ctx), StateNotify("SBDTransmit")
                {
                    if(context<SBD>().in_response_to_ring_alert())
                        context<Command>().push_at_command("+SBDIXA");
                    else
                        context<Command>().push_at_command("+SBDIX");
                }
                ~SBDTransmit()
                {
                    context<SBD>().clear_data();
                }

                std::string mo_status_as_string(int code)
                {
                    switch(code)
                    {
                        // success
                        case 0: return "MO message, if any, transferred successfully";
                        case 1: return "MO message, if any, transferred successfully, but the MT message in the queue was too big to be transferred";
                        case 2: return "MO message, if any, transferred successfully, but the requested Location Update was not accepted";
                        case 3: case 4:
                            return "Reserved, but indicate MO session success if used";

                            // failure
                        case 5: case 6: case 7: case 8: case 9:
                        case 20: case 21: case 22: case 23: case 24: case 25: case 26: case 27: case 28: case 29: case 30: case 31:
                        case 33: case 34:
                        case 36:
                        default:
                            return "Reserved, but indicate MO session failure if used";                            
                        case 10: return "GSS reported that the call did not complete in the allowed time";
                        case 11: return "MO message queue at the GSS is full";
                        case 12: return "MO message has too many segments";
                        case 13: return "GSS reported that the session did not complete";
                        case 14: return "Invalid segment size";
                        case 15: return "Access is denied";
                        case 16: return "Modem has been locked and may not make SBD calls";
                        case 17: return "Gateway not responding (local session timeout)";
                        case 18: return "Connection lost (RF drop)";
                        case 19: return "Link failure (A protocol error caused termination of the call)";
                        case 32: return "No network service, unable to initiate call";
                        case 35: return "Iridium 9523 is busy, unable to initiate call";
                    }
                }
                
                
                boost::statechart::result react( const EvSBDTransmitComplete& e)
                {
                    // +SBDIX:<MO status>,<MOMSN>,<MT status>,<MTMSN>,<MT length>,<MT queued>
                    std::vector<std::string> sbdi_fields;
                    boost::algorithm::split(sbdi_fields, e.sbdi_, boost::is_any_of(":,"));

                    std::for_each(sbdi_fields.begin(), sbdi_fields.end(), 
                                  boost::bind(&boost::trim<std::string>, _1, std::locale()));
                    
                    if(sbdi_fields.size() != 7)
                    {
                        glog.is(goby::common::logger::DEBUG1) && glog << group("iridiumdriver") << "Invalid +SBDI response: " << e.sbdi_ << std::endl;
                        return transit<SBDReady>();
                    }
                    else
                    {
                        
                        enum { MO_STATUS = 1, MOMSN = 2, MT_STATUS = 3, MTMSN = 4, MT_LENGTH=5, MT_QUEUED = 6 };
                        enum { MT_STATUS_NO_MESSAGE = 0, MT_STATUS_RECEIVED_MESSAGE = 1, MT_STATUS_ERROR = 2 } ;

                        // 0-4 OK, 5-36 FAILURE
                        enum { MO_STATUS_SUCCESS_MAX = 4, MO_STATUS_FAILURE_MIN = 5 };
                        
                        
                        
                        int mo_status = goby::util::as<int>(sbdi_fields[MO_STATUS]);
                        if(mo_status <= MO_STATUS_SUCCESS_MAX)
                        {
                            glog.is(goby::common::logger::DEBUG1) && glog << group("iridiumdriver") << "Success sending SBDIX: " << mo_status_as_string(mo_status) << std::endl;
                        }
                        else
                        {
                            glog.is(goby::common::logger::WARN) && glog << group("iridiumdriver") << "Error sending SBD packet: " << mo_status_as_string(mo_status) << std::endl;
                            return transit<SBDReady>();
                        }
                        
                            
                        int mt_status = goby::util::as<int>(sbdi_fields[MT_STATUS]);
                        if(mt_status == MT_STATUS_RECEIVED_MESSAGE)
                            return transit<SBDReceive>();
                        else
                            return transit<SBDReady>();
                    }
                    
                }
            };
            
            struct SBDReceive : boost::statechart::state<SBDReceive, SBD >, StateNotify
            {
                typedef boost::mpl::list<
                    boost::statechart::transition< EvSBDReceiveComplete, SBDReady >
                    > reactions;
                SBDReceive(my_context ctx) : my_base(ctx), StateNotify("SBDReceive") {
                    context<Command>().push_at_command("+SBDRB");
                }
                ~SBDReceive() {
                }
                
            };



            
        }
    }
}


#endif

