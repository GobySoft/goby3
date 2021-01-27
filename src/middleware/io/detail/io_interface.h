// Copyright 2019-2020:
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

#ifndef GOBY_MIDDLEWARE_IO_DETAIL_IO_INTERFACE_H
#define GOBY_MIDDLEWARE_IO_DETAIL_IO_INTERFACE_H

#include <chrono>    // for seconds
#include <exception> // for exception
#include <memory>    // for shared_ptr
#include <mutex>     // for mutex, lock_...
#include <ostream>   // for endl, size_t
#include <string>    // for string, oper...
#include <thread>    // for thread
#include <unistd.h>  // for usleep

#include <boost/asio/write.hpp>        // for async_write
#include <boost/system/error_code.hpp> // for error_code

#include "goby/exception.h"                           // for Exception
#include "goby/middleware/application/multi_thread.h" // for SimpleThread
#include "goby/middleware/common.h"                   // for thread_id
#include "goby/middleware/io/groups.h"                // for status
#include "goby/middleware/protobuf/io.pb.h"           // for IOError, IOS...
#include "goby/time/steady_clock.h"                   // for SteadyClock
#include "goby/util/asio_compat.h"
#include "goby/util/debug_logger.h" // for glog

#include "io_transporters.h"

namespace goby
{
namespace middleware
{
class Group;
class InterThreadTransporter;
template <typename InnerTransporter> class InterProcessForwarder;
namespace io
{
enum class ThreadState
{
    SUBSCRIPTIONS_COMPLETE
};

namespace detail
{
template <typename ProtobufEndpoint, typename ASIOEndpoint>
ProtobufEndpoint endpoint_convert(const ASIOEndpoint& asio_ep)
{
    ProtobufEndpoint pb_ep;
    pb_ep.set_addr(asio_ep.address().to_string());
    pb_ep.set_port(asio_ep.port());
    return pb_ep;
}

template <const goby::middleware::Group& line_in_group,
          const goby::middleware::Group& line_out_group, PubSubLayer publish_layer,
          PubSubLayer subscribe_layer, typename IOConfig, typename SocketType,
          template <class> class ThreadType, bool use_indexed_groups = false>
class IOThread : public ThreadType<IOConfig>,
                 public IOPublishTransporter<
                     IOThread<line_in_group, line_out_group, publish_layer, subscribe_layer,
                              IOConfig, SocketType, ThreadType, use_indexed_groups>,
                     line_in_group, publish_layer, use_indexed_groups>,
                 public IOSubscribeTransporter<
                     IOThread<line_in_group, line_out_group, publish_layer, subscribe_layer,
                              IOConfig, SocketType, ThreadType, use_indexed_groups>,
                     line_out_group, subscribe_layer, use_indexed_groups>
{
  public:
    /// \brief Constructs the thread.
    /// \param config A reference to the configuration read by the main application at launch
    /// \param index Thread index for multiple instances in a given application (-1 indicates a single instance)
    /// \param glog_group String name for group to use for glog
    IOThread(const IOConfig& config, int index, std::string glog_group = "i/o")
        : ThreadType<IOConfig>(config, this->loop_max_frequency(), index),
          IOPublishTransporter<
              IOThread<line_in_group, line_out_group, publish_layer, subscribe_layer, IOConfig,
                       SocketType, ThreadType, use_indexed_groups>,
              line_in_group, publish_layer, use_indexed_groups>(index),
          IOSubscribeTransporter<
              IOThread<line_in_group, line_out_group, publish_layer, subscribe_layer, IOConfig,
                       SocketType, ThreadType, use_indexed_groups>,
              line_out_group, subscribe_layer, use_indexed_groups>(index),
          glog_group_(glog_group + " / t" + goby::middleware::thread_id().substr(0, 6))
    {
        auto data_out_callback =
            [this](std::shared_ptr<const goby::middleware::protobuf::IOData> io_msg) {
                if (!io_msg->has_index() || io_msg->index() == this->index())
                {
                    write(io_msg);
                }
            };

        this->template subscribe_out<goby::middleware::protobuf::IOData>(data_out_callback);

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
        incoming_mail_notify_thread_.reset();
    }

    virtual ~IOThread()
    {
        socket_.reset();

        // for non clean shutdown, avoid abort
        if (incoming_mail_notify_thread_)
            incoming_mail_notify_thread_->detach();

        auto status = std::make_shared<protobuf::IOStatus>();
        status->set_state(protobuf::IO__LINK_CLOSED);

        this->publish_in(status);
        this->template unsubscribe_out<goby::middleware::protobuf::IOData>();
    }

    template <class IOThreadImplementation>
    friend void basic_async_write(IOThreadImplementation* this_thread,
                                  std::shared_ptr<const goby::middleware::protobuf::IOData> io_msg);

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

        this->publish_in(io_msg);
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

    boost::asio::io_context& mutable_io() { return io_; }

    /// \brief Does the socket exist and is it open?
    bool socket_is_open() { return socket_ && socket_->is_open(); }

    /// \brief Opens the newly created socket/serial_port
    virtual void open_socket() = 0;

    /// \brief Starts an asynchronous read on the socket.
    virtual void async_read() = 0;

    /// \brief Starts an asynchronous write from data published
    virtual void async_write(std::shared_ptr<const goby::middleware::protobuf::IOData> io_msg) = 0;

    const std::string& glog_group() { return glog_group_; }

  private:
    /// \brief Tries to open the socket, and if fails publishes an error
    void try_open();

    /// \brief If the socket is not open, try to open it. Otherwise, block until either 1) data is read or 2) we have incoming mail
    void loop() override;

  private:
    boost::asio::io_context io_;
    std::unique_ptr<SocketType> socket_;

    const goby::time::SteadyClock::duration min_backoff_interval_{std::chrono::seconds(1)};
    const goby::time::SteadyClock::duration max_backoff_interval_{std::chrono::seconds(128)};
    goby::time::SteadyClock::duration backoff_interval_{min_backoff_interval_};
    goby::time::SteadyClock::time_point next_open_attempt_{goby::time::SteadyClock::now()};

    std::mutex incoming_mail_notify_mutex_;
    std::unique_ptr<std::thread> incoming_mail_notify_thread_;

    std::string glog_group_;
    bool glog_group_added_{false};
};

template <class IOThreadImplementation>
void basic_async_write(IOThreadImplementation* this_thread,
                       std::shared_ptr<const goby::middleware::protobuf::IOData> io_msg)
{
    boost::asio::async_write(
        this_thread->mutable_socket(), boost::asio::buffer(io_msg->data()),
        // capture io_msg in callback to ensure write buffer exists until async_write is done
        [this_thread, io_msg](const boost::system::error_code& ec, std::size_t bytes_transferred) {
            if (!ec && bytes_transferred > 0)
            {
                this_thread->handle_write_success(bytes_transferred);
            }
            else
            {
                this_thread->handle_write_error(ec);
            }
        });
}

} // namespace detail
} // namespace io
} // namespace middleware
} // namespace goby

template <const goby::middleware::Group& line_in_group,
          const goby::middleware::Group& line_out_group,
          goby::middleware::io::PubSubLayer publish_layer,
          goby::middleware::io::PubSubLayer subscribe_layer, typename IOConfig, typename SocketType,
          template <class> class ThreadType, bool use_indexed_groups>
void goby::middleware::io::detail::IOThread<line_in_group, line_out_group, publish_layer,
                                            subscribe_layer, IOConfig, SocketType, ThreadType,
                                            use_indexed_groups>::try_open()
{
    try
    {
        socket_.reset(new SocketType(io_));
        open_socket();

        // messages read from the socket
        this->async_read();

        // reset io_context, which ran out of work
        io_.reset();

        // successful, reset backoff
        backoff_interval_ = min_backoff_interval_;

        auto status = std::make_shared<protobuf::IOStatus>();
        if (this->index() != -1)
            status->set_index(this->index());

        status->set_state(protobuf::IO__LINK_OPEN);
        this->publish_in(status);

        goby::glog.is_debug2() && goby::glog << group(glog_group_) << "Successfully opened socket"
                                             << std::endl;

        // update to avoid thrashing on open success but read/write failure
        decltype(next_open_attempt_) now(goby::time::SteadyClock::now());
        next_open_attempt_ = now + backoff_interval_;
    }
    catch (const std::exception& e)
    {
        auto status = std::make_shared<protobuf::IOStatus>();
        if (this->index() != -1)
            status->set_index(this->index());

        status->set_state(protobuf::IO__CRITICAL_FAILURE);
        goby::middleware::protobuf::IOError& error = *status->mutable_error();
        error.set_code(goby::middleware::protobuf::IOError::IO__INIT_FAILURE);
        error.set_text(e.what() + std::string(": config (") + this->cfg().ShortDebugString() + ")");
        this->publish_in(status);

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
        socket_.reset();
    }
}

template <const goby::middleware::Group& line_in_group,
          const goby::middleware::Group& line_out_group,
          goby::middleware::io::PubSubLayer publish_layer,
          goby::middleware::io::PubSubLayer subscribe_layer, typename IOConfig, typename SocketType,
          template <class> class ThreadType, bool use_indexed_groups>
void goby::middleware::io::detail::IOThread<line_in_group, line_out_group, publish_layer,
                                            subscribe_layer, IOConfig, SocketType, ThreadType,
                                            use_indexed_groups>::loop()
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
            usleep(100000); // avoid pegging CPU while waiting to attempt reopening socket
    }
}

template <const goby::middleware::Group& line_in_group,
          const goby::middleware::Group& line_out_group,
          goby::middleware::io::PubSubLayer publish_layer,
          goby::middleware::io::PubSubLayer subscribe_layer, typename IOConfig, typename SocketType,
          template <class> class ThreadType, bool use_indexed_groups>
void goby::middleware::io::detail::IOThread<
    line_in_group, line_out_group, publish_layer, subscribe_layer, IOConfig, SocketType, ThreadType,
    use_indexed_groups>::handle_read_error(const boost::system::error_code& ec)
{
    auto status = std::make_shared<protobuf::IOStatus>();
    if (this->index() != -1)
        status->set_index(this->index());

    status->set_state(protobuf::IO__CRITICAL_FAILURE);
    goby::middleware::protobuf::IOError& error = *status->mutable_error();
    error.set_code(goby::middleware::protobuf::IOError::IO__READ_FAILURE);
    error.set_text(ec.message());
    this->publish_in(status);

    goby::glog.is_warn() && goby::glog << group(glog_group_)
                                       << "Failed to read from the socket/serial_port: "
                                       << error.ShortDebugString() << std::endl;

    socket_.reset();
}

template <const goby::middleware::Group& line_in_group,
          const goby::middleware::Group& line_out_group,
          goby::middleware::io::PubSubLayer publish_layer,
          goby::middleware::io::PubSubLayer subscribe_layer, typename IOConfig, typename SocketType,
          template <class> class ThreadType, bool use_indexed_groups>
void goby::middleware::io::detail::IOThread<
    line_in_group, line_out_group, publish_layer, subscribe_layer, IOConfig, SocketType, ThreadType,
    use_indexed_groups>::handle_write_error(const boost::system::error_code& ec)
{
    auto status = std::make_shared<protobuf::IOStatus>();
    if (this->index() != -1)
        status->set_index(this->index());

    status->set_state(protobuf::IO__CRITICAL_FAILURE);
    goby::middleware::protobuf::IOError& error = *status->mutable_error();
    error.set_code(goby::middleware::protobuf::IOError::IO__WRITE_FAILURE);
    error.set_text(ec.message());
    this->publish_in(status);

    goby::glog.is_warn() && goby::glog << group(glog_group_)
                                       << "Failed to write to the socket/serial_port: "
                                       << error.ShortDebugString() << std::endl;
    socket_.reset();
}

#endif
