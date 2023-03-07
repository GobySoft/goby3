// Copyright 2009-2021:
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

#ifndef GOBY_ACOMMS_MODEMDRIVER_DRIVER_BASE_H
#define GOBY_ACOMMS_MODEMDRIVER_DRIVER_BASE_H

#include <atomic>                         // for atomic
#include <boost/signals2/signal.hpp>      // for signal
#include <boost/smart_ptr/shared_ptr.hpp> // for shared_ptr
#include <iosfwd>                         // for ofstream
#include <memory>                         // for shared_ptr, __shared_p...
#include <string>                         // for string

#include "goby/acomms/protobuf/driver_base.pb.h" // for DriverCo...

namespace goby
{
namespace util
{
class LineBasedInterface;
} // namespace util

namespace acomms
{
namespace protobuf
{
class DriverConfig;
class ModemRaw;
class ModemTransmission;
class ModemReport;
} // namespace protobuf

/// \class ModemDriverBase driver_base.h goby/acomms/modem_driver.h
/// \ingroup acomms_api
/// \brief provides an abstract base class for acoustic modem drivers. This is subclassed by the various drivers for different manufacturers' modems.
/// \sa acomms_driver_base.proto and acomms_modem_message.proto for definition of Google Protocol Buffers messages (namespace goby::acomms::protobuf).
class ModemDriverBase
{
  public:
    /// \name Control
    //@{

    /// \brief Starts the modem driver. Must be called before poll().
    ///
    /// \param cfg Startup configuration for the driver and modem. DriverConfig is defined in acomms_driver_base.proto. Derived classes can define extensions (see https://developers.google.com/protocol-buffers/docs/proto) to DriverConfig to handle modem specific configuration.
    virtual void startup(const protobuf::DriverConfig& cfg) = 0;

    /// \brief Update configuration while running (not required to be implemented)
    virtual void update_cfg(const protobuf::DriverConfig& cfg);

    /// \brief Shuts down the modem driver.
    virtual void shutdown() = 0;

    /// \brief Allows the modem driver to do its work.
    ///
    /// Should be called regularly to perform the work of the driver as the driver *does not* run in its own thread. This allows us to guarantee that no signals are called except inside this method. Does not block.
    virtual void do_work() = 0;

    //@}

    /// \name MAC Slots
    //@{
    /// \brief Virtual initiate_transmission method. Typically connected to MACManager::signal_initiate_transmission() using bind().
    ///
    /// \param m ModemTransmission (defined in acomms_modem_message.proto) containing the details of the transmission to be started. This may contain data frames. If not, data will be requested when the driver calls the data request signal (ModemDriverBase::signal_data_request)
    virtual void handle_initiate_transmission(const protobuf::ModemTransmission& m) = 0;
    //@}

    /// \name MAC / Queue Signals
    //@{

    /// \brief Called when a binary data transmission is received from the modem
    ///
    /// You should connect one or more slots (a function or member function) to this signal to receive incoming messages. Use the goby::acomms::connect family of functions to do this. This signal will only be called during a call to poll. ModemDataTransmission is defined in acomms_modem_message.proto.
    boost::signals2::signal<void(const protobuf::ModemTransmission& message)> signal_receive;

    /// \brief Called when a transmission is completed.
    ///
    /// You should connect one or more slots (a function or member function) to this signal to receive incoming messages. Use the goby::acomms::connect family of functions to do this. This signal will only be called during a call to poll. ModemDataTransmission is defined in acomms_modem_message.proto.
    boost::signals2::signal<void(const protobuf::ModemTransmission& message)>
        signal_transmit_result;

    /// \brief Called when the modem or modem driver needs data to send. The returned data should be stored in ModemTransmission::frame
    ///
    /// You should connect one or more slots (a function or member function) to this signal to handle data requests. Use the goby::acomms::connect family of functions to do this. This signal will only be called during a call to poll. ModemTransmission is defined in acomms_modem_message.proto.
    boost::signals2::signal<void(protobuf::ModemTransmission* msg)> signal_data_request;

    /// \brief Called before the modem driver begins processing a transmission. This allows a third party to modify the parameters of the transmission (such as destination or rate) on the fly.
    ///
    /// You may connect one or more slots (a function or member function) to this signal to handle data requests. Use the goby::acomms::connect family of functions to do this. This signal will only be called during a call to poll. ModemTransmission is defined in acomms_modem_message.proto.
    boost::signals2::signal<void(protobuf::ModemTransmission* msg_request)>
        signal_modify_transmission;

    /// \brief Called after any message is received from the modem by the driver. Used by the MACManager for auto-discovery of vehicles. Also useful for higher level analysis and debugging of the transactions between the driver and the modem.
    ///
    /// If desired, you should connect one or more slots (a function or member function) to this signal to listen on incoming transactions. Use the goby::acomms::connect family of functions to do this. This signal will only be called during a call to poll. ModemRaw is defined in acomms_modem_message.proto.
    boost::signals2::signal<void(const protobuf::ModemRaw& msg)> signal_raw_incoming;

    /// \brief Called after any message is sent from the driver to the modem. Useful for higher level analysis and debugging of the transactions between the driver and the modem.
    ///
    /// If desired, you should connect one or more slots (a function or member function) to this signal to listen on outgoing transactions. Use the goby::acomms::connect family of functions to do this. This signal will only be called during a call to poll. ModemRaw is defined in acomms_modem_message.proto.
    boost::signals2::signal<void(const protobuf::ModemRaw& msg)> signal_raw_outgoing;
    //@}

    /// Public Destructor
    virtual ~ModemDriverBase();

    /// \name Informational
    //@{

    /// \brief Returns report including modem availability and signal quality (if known)
    virtual void report(protobuf::ModemReport* report);

    /// \brief Integer for the order in which this driver was started (first driver started is 1, second driver is 2, etc.)
    int driver_order() { return order_; }

    /// \brief Unique driver name (e.g. UDP_MULTICAST::1 or my_driver_name::2)
    ///
    /// The name has two parts separated by "::". The first part is the driver_type enum without the "DRIVER_" prefix or the result of goby_driver_name() function (for plugin drivers). The second part is the modem id.
    static std::string driver_name(const protobuf::DriverConfig& cfg);
    //@}

  protected:
    /// \name Constructors/Destructor
    //@{

    /// \brief Constructor
    ModemDriverBase();

    //@}

    /// \name Write/read from the line-based interface to the modem
    //@{

    /// \brief write a line to the serial port.
    ///
    /// \param out reference to string to write. Must already include any end-of-line character(s).
    void modem_write(const std::string& out);

    /// \brief read a line from the serial port, including end-of-line character(s)
    ///
    /// \param in pointer to string to store line
    /// \return true if a line was available, false if no line available
    bool modem_read(std::string* in);

    /// \brief start the physical connection to the modem (serial port, TCP, etc.). must be called before ModemDriverBase::modem_read() or ModemDriverBase::modem_write()
    ///
    /// \param cfg Configuration including the parameters for the physical connection. (protobuf::DriverConfig is defined in acomms_driver_base.proto).
    /// \throw ModemDriverException Problem opening the physical connection.
    ///
    void modem_start(const protobuf::DriverConfig& cfg);

    /// \brief closes the serial port. Use modem_start to reopen the port.
    void modem_close();

    const std::string& glog_out_group() const { return glog_out_group_; }
    const std::string& glog_in_group() const { return glog_in_group_; }

    /// \brief use for direct access to the modem
    util::LineBasedInterface& modem() { return *modem_; }

    //@}
  protected:
    static std::atomic<int> count_;

  private:
    void write_raw(const protobuf::ModemRaw& msg, bool rx);

  private:
    // represents the line based communications interface to the modem
    std::shared_ptr<util::LineBasedInterface> modem_;

    std::string glog_out_group_;
    std::string glog_in_group_;

    std::shared_ptr<std::ofstream> raw_fs_;
    bool raw_fs_connections_made_{false};
    int order_;

    protobuf::DriverConfig cfg_;
};
} // namespace acomms
} // namespace goby
#endif
