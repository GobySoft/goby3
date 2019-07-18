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

#ifndef SerialInterface20190718H
#define SerialInterface20190718H

#include <boost/asio/read_until.hpp>
#include <boost/asio/serial_port.hpp>
#include <boost/asio/streambuf.hpp>
#include <boost/asio/system_timer.hpp>
#include <boost/asio/write.hpp>
#include <boost/units/systems/si/prefixes.hpp>

#include <goby/common/time.h>
#include <goby/middleware/application/multi_thread.h>

namespace goby
{
namespace middleware
{
namespace io
{
template <const goby::Group& line_in_group, const goby::Group& line_out_group>
class SerialThread : public goby::SimpleThread<goby::middleware::protobuf::SerialConfig>
{
    using Base = goby::SimpleThread<goby::middleware::protobuf::SerialConfig>;

  public:
    /// \brief Constructs the thread.
    /// \param config A reference to the Protocol Buffers config read by the main application at launch
    SerialThread(const goby::middleware::protobuf::SerialConfig& config)
        : Base(config, this->loop_max_frequency()), timer_(io_)
    {
        // messages to write to the serial port
        this->interthread().template subscribe<line_out_group, goby::middleware::protobuf::IOData>(
            [this](std::shared_ptr<const goby::middleware::protobuf::IOData> io_msg) {
                goby::glog.is_debug2() && goby::glog << group("i/o") << "(" << io_msg->data().size()
                                                     << "B) < " << io_msg->ShortDebugString()
                                                     << std::endl;
                this->async_write(io_msg->data());
            });

        this->interthread()
            .template subscribe<line_out_group, goby::middleware::protobuf::SerialCommand>(
                [this](std::shared_ptr<const goby::middleware::protobuf::SerialCommand> cmd) {
                    goby::glog.is_debug2() && goby::glog << group("i/o") << "< [Command] "
                                                         << cmd->ShortDebugString() << std::endl;
                    switch (cmd->command())
                    {
                        case protobuf::SerialCommand::SEND_BREAK:
                            if (serial_port_ && serial_port_->is_open())
                                serial_port_->send_break();
                            break;

                            // sets RTS high, needed for PHSEN and PCO2W comms
                        case protobuf::SerialCommand::RTS_HIGH:
                            if (serial_port_ && serial_port_->is_open())
                            {
                                int fd = serial_port_->native_handle();
                                int RTS_flag = TIOCM_RTS;
                                // TIOCMBIS - set bit
                                ioctl(fd, TIOCMBIS, &RTS_flag);
                            }
                            break;

                        case protobuf::SerialCommand::RTS_LOW:
                            if (serial_port_ && serial_port_->is_open())
                            {
                                int fd = serial_port_->native_handle();
                                int RTS_flag = TIOCM_RTS;
                                // TIOCMBIC - clear bit
                                ioctl(fd, TIOCMBIC, &RTS_flag);
                            }
                            break;
                    }
                });

        static bool io_group_added = false;
        if (!io_group_added)
        {
            goby::glog.add_group("i/o", goby::common::Colors::red);
            io_group_added = true;
        }
    }

    ~SerialThread()
    {
        serial_port_.reset();

        protobuf::IOStatus status;
        status.set_state(protobuf::IO__LINK_CLOSED);
        Base::interthread().template publish<goby::middleware::io::groups::status>(status);

        this->interthread()
            .template unsubscribe<line_out_group, goby::middleware::protobuf::IOData>();
        this->interthread()
            .template unsubscribe<line_out_group, goby::middleware::protobuf::SerialCommand>();
    }

  protected:
    void handle_read_success(std::size_t bytes_transferred, const std::string& bytes)
    {
        auto io_msg = std::make_shared<goby::middleware::protobuf::IOData>();
        *io_msg->data() = bytes;

        goby::glog.is_debug2() && goby::glog << group("i/o") << "(" << bytes_transferred << "B) > "
                                             << io_msg->ShortDebugString() << std::endl;

        this->interprocess().template publish<line_in_group>(io_msg);
        this->async_read();
    }

    void handle_write_success(std::size_t bytes_transferred) {}

    void handle_read_error(const boost::system::error_code& ec);
    void handle_write_error(const boost::system::error_code& ec);

    /// \brief Access the (mutable) serial_port object
    boost::asio::serial_port& mutable_serial_port()
    {
        if (serial_port_)
            return *serial_port_;
        else
            throw IOError("Attempted to access null serial_port");
    }

    /// \brief Starts an asynchronous read on the serial port.
    virtual void async_read() = 0;

    /// \brief Starts an asynchronous write from data published
    virtual void async_write(const std::string& bytes);

  private:
    /// \brief Tries to open the serial port, and if fails publishes an error
    void try_open();

    /// \brief Sets a timer used to ensure that messages are sent to the serial device occasionally, even if no data is read
    void set_timer();

    /// \brief If the serial port is not open, try to open it. Otherwise, block until either 1) data is read or 2) the timer expires.
    void loop() override;

  private:
    boost::asio::io_service io_;
    boost::asio::system_timer timer_;
    std::unique_ptr<boost::asio::serial_port> serial_port_;

    const boost::units::quantity<boost::units::si::time> min_backoff_interval_{
        1 * boost::units::si::seconds};
    const boost::units::quantity<boost::units::si::time> max_backoff_interval_{
        128 * boost::units::si::seconds};
    boost::units::quantity<boost::units::si::time> backoff_interval_{min_backoff_interval_};
    boost::units::quantity<boost::units::si::time> next_open_attempt_{0 *
                                                                      boost::units::si::seconds};
};
} // namespace io
} // namespace middleware
} // namespace goby

template <const goby::Group& line_in_group, const goby::Group& line_out_group>
void goby::middleware::io::SerialThread<line_in_group, line_out_group>::try_open()
{
    try
    {
        serial_port_.reset(new boost::asio::serial_port(io_));

        serial_port_->open(cfg().port());
        using boost::asio::serial_port_base;
        serial_port_->set_option(serial_port_base::baud_rate(cfg().baud()));

        switch (cfg().flow_control())
        {
            case goby::middleware::protobuf::SerialConfig::NONE:
                serial_port_->set_option(
                    serial_port_base::flow_control(serial_port_base::flow_control::none));
                break;
            case goby::middleware::protobuf::SerialConfig::SOFTWARE:
                serial_port_->set_option(
                    serial_port_base::flow_control(serial_port_base::flow_control::software));
                break;
            case goby::middleware::protobuf::SerialConfig::HARDWARE:
                serial_port_->set_option(
                    serial_port_base::flow_control(serial_port_base::flow_control::hardware));
                break;
        }

        // 8N1
        serial_port_->set_option(serial_port_base::character_size(8));
        serial_port_->set_option(serial_port_base::parity(serial_port_base::parity::none));
        serial_port_->set_option(serial_port_base::stop_bits(serial_port_base::stop_bits::one));

        set_timer();

        // messages read from the serial port
        this->async_read();

        // successful, reset backoff
        backoff_interval_ = min_backoff_interval_;

        protobuf::IOStatus status;
        status.set_state(protobuf::IO__LINK_OPEN);
        Base::interthread().template publish<goby::middleware::io::groups::status>(status);
    }
    catch (const boost::system::system_error& e)
    {
        protobuf::IOStatus status;
        status.set_state(protobuf::IO__CRITICAL_FAILURE);
        goby::middleware::protobuf::IOError& error = *status.mutable_error();
        error.set_code(goby::middleware::protobuf::IOError::IO__SERIAL_PORT_INIT_FAILURE);
        error.set_text(e.what() + std::string(": config (") + cfg().ShortDebugString() + ")");
        Base::interthread().template publish<goby::middleware::io::groups::status>(status);

        goby::glog.is(goby::common::logger::WARN) &&
            goby::glog << group("i/o")
                       << "Failed to open/configure serial port: " << error.ShortDebugString()
                       << std::endl;

        if (backoff_interval_ < max_backoff_interval_)
            backoff_interval_ *= 2.0;

        decltype(next_open_attempt_) now(goby::time::now());
        next_open_attempt_ = now + backoff_interval_;

        goby::glog.is(goby::common::logger::WARN) &&
            goby::glog << group("i/o") << "Will retry in "
                       << backoff_interval_ / boost::units::si::seconds << " seconds" << std::endl;
    }
}

template <const goby::Group& line_in_group, const goby::Group& line_out_group>
void goby::middleware::io::SerialThread<line_in_group, line_out_group>::set_timer()
{
    // when the timer expires, stop the io_service to enable loop() to exit, and thus check any mail we may have
    // this ensures outgoing commands are sent eventually even if the serial port doesn't receive any data
    timer_.expires_from_now(std::chrono::milliseconds(cfg().out_mail_max_interval_ms()));
    timer_.async_wait([this](const boost::system::error_code& ec) { set_timer(); });
}

template <const goby::Group& line_in_group, const goby::Group& line_out_group>
void goby::middleware::io::SerialThread<line_in_group, line_out_group>::loop()
{
    if (serial_port_ && serial_port_->is_open())
    {
        // run the io service (until either we read something
        // from the serial port or the timer expires)
        io_.run_one();
    }
    else
    {
        decltype(next_open_attempt_) now(goby::time::now());
        if (now > next_open_attempt_)
            try_open();
        else
            usleep(10000); // avoid pegging CPU
    }
}

template <const goby::Group& line_in_group, const goby::Group& line_out_group>
void goby::middleware::io::SerialThread<line_in_group, line_out_group>::async_write(
    const std::string& bytes)
{
    if (bytes.empty())
        return;
    if (!serial_port_ || !serial_port_->is_open())
        return;

    boost::asio::async_write(
        *serial_port_, boost::asio::buffer(bytes),
        [this](const boost::system::error_code& ec, std::size_t bytes_transferred) {
            if (!ec && bytes_transferred > 0)
            {
                handle_write_success(bytes_transferred);
            }
            else
            {
                handle_write_error(ec);
            }
        });
}

template <const goby::Group& line_in_group, const goby::Group& line_out_group>
void goby::middleware::io::SerialThread<line_in_group, line_out_group>::handle_read_error(
    const boost::system::error_code& ec)
{
    protobuf::IOStatus status;
    status.set_state(protobuf::IO__CRITICAL_FAILURE);
    goby::middleware::protobuf::IOError& error = *status.mutable_error();
    error.set_code(goby::middleware::protobuf::IOError::IO__SERIAL_PORT_READ_FAILURE);
    error.set_text(ec.message());
    Base::interthread().template publish<goby::middleware::io::groups::status>(status);

    goby::glog.is(goby::common::logger::WARN) &&
        goby::glog << group("i/o")
                   << "Failed to read from the serial port: " << error.ShortDebugString()
                   << std::endl;

    serial_port_.reset();
}

template <const goby::Group& line_in_group, const goby::Group& line_out_group>
void goby::middleware::io::SerialThread<line_in_group, line_out_group>::handle_write_error(
    const boost::system::error_code& ec)
{
    protobuf::IOStatus status;
    status.set_state(protobuf::IO__CRITICAL_FAILURE);
    goby::middleware::protobuf::IOError& error = *status.mutable_error();
    error.set_code(goby::middleware::protobuf::IOError::IO__SERIAL_PORT_WRITE_FAILURE);
    error.set_text(ec.message());
    Base::interthread().template publish<goby::middleware::io::groups::status>(status);

    goby::glog.is(goby::common::logger::WARN) &&
        goby::glog << group("i/o")
                   << "Failed to write to the serial port: " << error.ShortDebugString()
                   << std::endl;
    serial_port_.reset();
}

#endif
