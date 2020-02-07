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
    /// \param config A reference to the configuration read by the main application at launch
    /// \param index Thread index for multiple instances in a given application (-1 indicates a single instance)
    IOThread(const IOConfig& config, int index = -1, std::string glog_group = "i/o")
        : goby::middleware::SimpleThread<IOConfig>(config, this->loop_max_frequency(), index),
          glog_group_(glog_group)
    {
        auto data_out_callback =
            [this](std::shared_ptr<const goby::middleware::protobuf::IOData> io_msg) {
                if (io_msg->index() == this->index())
                    write(io_msg);
            };

        this->subscribe_transporter()
            .template subscribe<line_out_group, goby::middleware::protobuf::IOData>(
                data_out_callback);

        if (!glog_group_added_)
        {
            goby::glog.add_group(glog_group_, goby::util::Colors::red);
            glog_group_added_ = true;
        }
    }

    void initialize() override
    {
        // thread to handle synchonization between boost::asio and goby condition_variable signaling
        incoming_mail_notify_thread_.reset(new std::thread([this]() {
            while (this->alive())
            {
                std::unique_lock<std::mutex> lock(incoming_mail_notify_mutex_);
                this->interthread().cv()->wait(lock);
                // post empty handler to cause loop() to return and allow incoming mail to be handled
                io_.post([]() {});
            }
        }));
    }

    void finalize() override
    {
        // join incoming mail thread
        {
            std::lock_guard<std::mutex> l(incoming_mail_notify_mutex_);
            this->interthread().cv()->notify_all();
        }
        incoming_mail_notify_thread_->join();
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
        goby::glog.is_debug2() &&
            goby::glog << group(glog_group_) << "(" << io_msg->data().size() << "B) <"
                       << ((this->index() == -1) ? std::string() : std::to_string(this->index()))
                       << " " << io_msg->ShortDebugString() << std::endl;
        if (io_msg->data().empty())
            return;
        if (!socket_ || !socket_->is_open())
            return;

        this->async_write(io_msg);
    }

    void handle_read_success(std::size_t bytes_transferred, const std::string& bytes)
    {
        auto io_msg = std::make_shared<goby::middleware::protobuf::IOData>();
        *io_msg->mutable_data() = bytes;

        handle_read_success(bytes_transferred, io_msg);
    }

    void handle_read_success(std::size_t bytes_transferred,
                             std::shared_ptr<goby::middleware::protobuf::IOData> io_msg)
    {
        if (this->index() != -1)
            io_msg->set_index(this->index());

        goby::glog.is_debug2() &&
            goby::glog << group(glog_group_) << "(" << bytes_transferred << "B) >"
                       << ((this->index() == -1) ? std::string() : std::to_string(this->index()))
                       << " " << io_msg->ShortDebugString() << std::endl;

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
    virtual void async_write(const std::string& bytes)
    {
        throw(goby::Exception(
            "Must overload async_write(const std::string& bytes) if not overloading "
            "async_write(std::shared_ptr<const goby::middleware::protobuf::IOData> io_msg)"));
    }

    /// \brief Starts an asynchronous write from data published
    virtual void async_write(std::shared_ptr<const goby::middleware::protobuf::IOData> io_msg)
    {
        async_write(io_msg->data());
    }

    const std::string& glog_group() { return glog_group_; }

  private:
    /// \brief Tries to open the socket, and if fails publishes an error
    void try_open();

    /// \brief If the socket is not open, try to open it. Otherwise, block until either 1) data is read or 2) we have incoming mail
    void loop() override;

  private:
    boost::asio::io_service io_;
    std::unique_ptr<SocketType> socket_;

    const goby::time::SteadyClock::duration min_backoff_interval_{std::chrono::seconds(1)};
    const goby::time::SteadyClock::duration max_backoff_interval_{std::chrono::seconds(128)};
    goby::time::SteadyClock::duration backoff_interval_{min_backoff_interval_};
    goby::time::SteadyClock::time_point next_open_attempt_{goby::time::SteadyClock::now()};

    std::mutex incoming_mail_notify_mutex_;
    std::unique_ptr<std::thread> incoming_mail_notify_thread_;

    std::string glog_group_;
    bool glog_group_added_{false};
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

        // messages read from the socket
        this->async_read();

        // reset io_service, which ran out of work
        io_.reset();

        // successful, reset backoff
        backoff_interval_ = min_backoff_interval_;

        protobuf::IOStatus status;
        status.set_state(protobuf::IO__LINK_OPEN);
        this->interthread().template publish<goby::middleware::io::groups::status>(status);

        goby::glog.is_debug2() && goby::glog << group(glog_group_) << "Successfully opened socket"
                                             << std::endl;
    }
    catch (const boost::system::system_error& e)
    {
        protobuf::IOStatus status;
        status.set_state(protobuf::IO__CRITICAL_FAILURE);
        goby::middleware::protobuf::IOError& error = *status.mutable_error();
        error.set_code(goby::middleware::protobuf::IOError::IO__INIT_FAILURE);
        error.set_text(e.what() + std::string(": config (") + this->cfg().ShortDebugString() + ")");
        this->interthread().template publish<goby::middleware::io::groups::status>(status);

        goby::glog.is_warn() && goby::glog << group(glog_group_)
                                           << "Failed to open/configure socket/serial_port: "
                                           << error.ShortDebugString() << std::endl;

        if (backoff_interval_ < max_backoff_interval_)
            backoff_interval_ *= 2.0;

        decltype(next_open_attempt_) now(goby::time::SteadyClock::now());
        next_open_attempt_ = now + backoff_interval_;

        goby::glog.is_warn() && goby::glog << group(glog_group_) << "Will retry in "
                                           << backoff_interval_ / std::chrono::seconds(1)
                                           << " seconds" << std::endl;
    }
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
        // run the io service (blocks until either we read something
        // from the socket or a subscription is available
        // as signaled from an empty handler in the incoming_mail_notify_thread)
        io_.run_one();
    }
    else
    {
        decltype(next_open_attempt_) now(goby::time::SteadyClock::now());
        if (now > next_open_attempt_)
            try_open();
        else
            usleep(10000); // avoid pegging CPU while waiting to attempt reopening socket
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

    goby::glog.is_warn() && goby::glog << group(glog_group_)
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

    goby::glog.is_warn() && goby::glog << group(glog_group_)
                                       << "Failed to write to the socket/serial_port: "
                                       << error.ShortDebugString() << std::endl;
    socket_.reset();
}

} // namespace io
} // namespace middleware
} // namespace goby

#endif
