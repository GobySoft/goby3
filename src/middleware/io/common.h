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

#ifndef IO_COMMON_20190815H
#define IO_COMMON_20190815H

#include <boost/asio/io_service.hpp>
#include <boost/asio/system_timer.hpp>

#include "goby/exception.h"
#include "goby/middleware/application/multi_thread.h"
#include "goby/middleware/io/groups.h"
#include "goby/middleware/protobuf/io.pb.h"
#include "goby/time/steady_clock.h"

namespace goby
{
namespace middleware
{
namespace io
{
enum class PubSubLayer
{
    INTERTHREAD,
    INTERPROCESS
};

template <class Derived, PubSubLayer layer> struct IOPublishTransporter
{
};

template <class Derived> struct IOPublishTransporter<Derived, PubSubLayer::INTERTHREAD>
{
    InterThreadTransporter& publish_transporter()
    {
        return static_cast<Derived*>(this)->interthread();
    }
};

template <class Derived> struct IOPublishTransporter<Derived, PubSubLayer::INTERPROCESS>
{
    InterProcessForwarder<InterThreadTransporter>& publish_transporter()
    {
        return static_cast<Derived*>(this)->interprocess();
    }
};

template <class Derived, PubSubLayer layer> struct IOSubscribeTransporter
{
};

template <class Derived> struct IOSubscribeTransporter<Derived, PubSubLayer::INTERTHREAD>
{
    InterThreadTransporter& subscribe_transporter()
    {
        return static_cast<Derived*>(this)->interthread();
    }
};

template <class Derived> struct IOSubscribeTransporter<Derived, PubSubLayer::INTERPROCESS>
{
    InterProcessForwarder<InterThreadTransporter>& subscribe_transporter()
    {
        return static_cast<Derived*>(this)->interprocess();
    }
};

template <const goby::middleware::Group& line_in_group,
          const goby::middleware::Group& line_out_group, PubSubLayer publish_layer,
          PubSubLayer subscribe_layer, typename IOConfig, typename SocketType>
class IOThread
    : public goby::middleware::SimpleThread<IOConfig>,
      public IOPublishTransporter<IOThread<line_in_group, line_out_group, publish_layer,
                                           subscribe_layer, IOConfig, SocketType>,
                                  publish_layer>,
      public IOSubscribeTransporter<IOThread<line_in_group, line_out_group, publish_layer,
                                             subscribe_layer, IOConfig, SocketType>,
                                    subscribe_layer>
{
  public:
    /// \brief Constructs the thread.
    /// \param config A reference to the Protocol Buffers config read by the main application at launch
    IOThread(const IOConfig& config)
        : goby::middleware::SimpleThread<IOConfig>(config, this->loop_max_frequency()), timer_(io_)
    {
        auto data_out_callback =
            [this](std::shared_ptr<const goby::middleware::protobuf::IOData> io_msg) {
                write(io_msg);
            };

        this->subscribe_transporter()
            .template subscribe<line_out_group, goby::middleware::protobuf::IOData>(
                data_out_callback);

        static bool io_group_added = false;
        if (!io_group_added)
        {
            goby::glog.add_group("i/o", goby::util::Colors::red);
            io_group_added = true;
        }
    }

    ~IOThread()
    {
        socket_.reset();

        protobuf::IOStatus status;
        status.set_state(protobuf::IO__LINK_CLOSED);
        this->interthread().template publish<goby::middleware::io::groups::status>(status);

        this->subscribe_transporter()
            .template unsubscribe<line_out_group, goby::middleware::protobuf::IOData>();
    }

  protected:
    void write(std::shared_ptr<const goby::middleware::protobuf::IOData> io_msg)
    {
        goby::glog.is_debug2() && goby::glog << group("i/o") << "(" << io_msg->data().size()
                                             << "B) < " << io_msg->ShortDebugString() << std::endl;
        if (io_msg->data().empty())
            return;
        if (!socket_ || !socket_->is_open())
            return;

        this->async_write(io_msg->data());
    }

    void handle_read_success(std::size_t bytes_transferred, const std::string& bytes)
    {
        auto io_msg = std::make_shared<goby::middleware::protobuf::IOData>();
        *io_msg->mutable_data() = bytes;

        goby::glog.is_debug2() && goby::glog << group("i/o") << "(" << bytes_transferred << "B) > "
                                             << io_msg->ShortDebugString() << std::endl;

        this->publish_transporter().template publish<line_in_group>(io_msg);
    }

    void handle_write_success(std::size_t bytes_transferred) {}
    void handle_read_error(const boost::system::error_code& ec);
    void handle_write_error(const boost::system::error_code& ec);

    /// \brief Access the (mutable) socket (or serial_port) object
    SocketType& mutable_socket()
    {
        if (socket_)
            return *socket_;
        else
            throw goby::Exception("Attempted to access null socket/serial_port");
    }

    boost::asio::io_service& mutable_io() { return io_; }

    /// \brief Does the socket exist and is it open?
    bool socket_is_open() { return socket_ && socket_->is_open(); }

    /// \brief Opens the newly created socket/serial_port
    virtual void open_socket() = 0;

    /// \brief Starts an asynchronous read on the socket.
    virtual void async_read() = 0;

    /// \brief Starts an asynchronous write from data published
    virtual void async_write(const std::string& bytes) = 0;

  private:
    /// \brief Tries to open the socket, and if fails publishes an error
    void try_open();

    /// \brief Sets a timer used to ensure that messages are sent to the serial device occasionally, even if no data is read
    void set_timer();

    /// \brief If the socket is not open, try to open it. Otherwise, block until either 1) data is read or 2) the timer expires.
    void loop() override;

  private:
    boost::asio::io_service io_;
    boost::asio::system_timer timer_;
    std::unique_ptr<SocketType> socket_;

    const goby::time::SteadyClock::duration min_backoff_interval_{std::chrono::seconds(1)};
    const goby::time::SteadyClock::duration max_backoff_interval_{std::chrono::seconds(128)};
    goby::time::SteadyClock::duration backoff_interval_{min_backoff_interval_};
    goby::time::SteadyClock::time_point next_open_attempt_{goby::time::SteadyClock::now()};
}; // namespace io

template <const goby::middleware::Group& line_in_group,
          const goby::middleware::Group& line_out_group,
          goby::middleware::io::PubSubLayer publish_layer,
          goby::middleware::io::PubSubLayer subscribe_layer, typename IOConfig, typename SocketType>
void goby::middleware::io::IOThread<line_in_group, line_out_group, publish_layer, subscribe_layer,
                                    IOConfig, SocketType>::try_open()
{
    try
    {
        socket_.reset(new SocketType(io_));
        open_socket();
        set_timer();

        // messages read from the socket
        this->async_read();

        // successful, reset backoff
        backoff_interval_ = min_backoff_interval_;

        protobuf::IOStatus status;
        status.set_state(protobuf::IO__LINK_OPEN);
        this->interthread().template publish<goby::middleware::io::groups::status>(status);
    }
    catch (const boost::system::system_error& e)
    {
        protobuf::IOStatus status;
        status.set_state(protobuf::IO__CRITICAL_FAILURE);
        goby::middleware::protobuf::IOError& error = *status.mutable_error();
        error.set_code(goby::middleware::protobuf::IOError::IO__INIT_FAILURE);
        error.set_text(e.what() + std::string(": config (") + this->cfg().ShortDebugString() + ")");
        this->interthread().template publish<goby::middleware::io::groups::status>(status);

        goby::glog.is_warn() && goby::glog << group("i/o")
                                           << "Failed to open/configure socket/serial_port: "
                                           << error.ShortDebugString() << std::endl;

        if (backoff_interval_ < max_backoff_interval_)
            backoff_interval_ *= 2.0;

        decltype(next_open_attempt_) now(goby::time::SteadyClock::now());
        next_open_attempt_ = now + backoff_interval_;

        goby::glog.is_warn() && goby::glog << group("i/o") << "Will retry in "
                                           << backoff_interval_ / std::chrono::seconds(1)
                                           << " seconds" << std::endl;
    }
}

template <const goby::middleware::Group& line_in_group,
          const goby::middleware::Group& line_out_group,
          goby::middleware::io::PubSubLayer publish_layer,
          goby::middleware::io::PubSubLayer subscribe_layer, typename IOConfig, typename SocketType>
void goby::middleware::io::IOThread<line_in_group, line_out_group, publish_layer, subscribe_layer,
                                    IOConfig, SocketType>::set_timer()
{
    // when the timer expires, stop the io_service to enable loop() to exit, and thus check any mail we may have
    // this ensures outgoing commands are sent eventually even if the socket doesn't receive any data
    timer_.expires_from_now(std::chrono::milliseconds(this->cfg().out_mail_max_interval_ms()));
    timer_.async_wait([this](const boost::system::error_code& ec) { set_timer(); });
}

template <const goby::middleware::Group& line_in_group,
          const goby::middleware::Group& line_out_group,
          goby::middleware::io::PubSubLayer publish_layer,
          goby::middleware::io::PubSubLayer subscribe_layer, typename IOConfig, typename SocketType>
void goby::middleware::io::IOThread<line_in_group, line_out_group, publish_layer, subscribe_layer,
                                    IOConfig, SocketType>::loop()
{
    if (socket_ && socket_->is_open())
    {
        // run the io service (until either we read something
        // from the socket or the timer expires)
        io_.run_one();
    }
    else
    {
        decltype(next_open_attempt_) now(goby::time::SteadyClock::now());
        if (now > next_open_attempt_)
            try_open();
        else
            usleep(10000); // avoid pegging CPU
    }
}

template <const goby::middleware::Group& line_in_group,
          const goby::middleware::Group& line_out_group,
          goby::middleware::io::PubSubLayer publish_layer,
          goby::middleware::io::PubSubLayer subscribe_layer, typename IOConfig, typename SocketType>
void goby::middleware::io::IOThread<
    line_in_group, line_out_group, publish_layer, subscribe_layer, IOConfig,
    SocketType>::handle_read_error(const boost::system::error_code& ec)
{
    protobuf::IOStatus status;
    status.set_state(protobuf::IO__CRITICAL_FAILURE);
    goby::middleware::protobuf::IOError& error = *status.mutable_error();
    error.set_code(goby::middleware::protobuf::IOError::IO__READ_FAILURE);
    error.set_text(ec.message());
    this->interthread().template publish<goby::middleware::io::groups::status>(status);

    goby::glog.is_warn() && goby::glog << group("i/o")
                                       << "Failed to read from the socket/serial_port: "
                                       << error.ShortDebugString() << std::endl;

    socket_.reset();
}

template <const goby::middleware::Group& line_in_group,
          const goby::middleware::Group& line_out_group,
          goby::middleware::io::PubSubLayer publish_layer,
          goby::middleware::io::PubSubLayer subscribe_layer, typename IOConfig, typename SocketType>
void goby::middleware::io::IOThread<
    line_in_group, line_out_group, publish_layer, subscribe_layer, IOConfig,
    SocketType>::handle_write_error(const boost::system::error_code& ec)
{
    protobuf::IOStatus status;
    status.set_state(protobuf::IO__CRITICAL_FAILURE);
    goby::middleware::protobuf::IOError& error = *status.mutable_error();
    error.set_code(goby::middleware::protobuf::IOError::IO__WRITE_FAILURE);
    error.set_text(ec.message());
    this->interthread().template publish<goby::middleware::io::groups::status>(status);

    goby::glog.is_warn() && goby::glog << group("i/o")
                                       << "Failed to write to the socket/serial_port: "
                                       << error.ShortDebugString() << std::endl;
    socket_.reset();
}

} // namespace io
} // namespace middleware
} // namespace goby

#endif
