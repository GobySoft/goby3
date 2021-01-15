// Copyright 2010-2020:
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

#include <ostream> // for basic_ostream
#include <utility> // for move

#include <boost/asio/error.hpp>                      // for host_not_found
#include <boost/asio/ip/tcp.hpp>
#include <boost/system/error_code.hpp>               // for error_code

#include "goby/util/as.h" // for as
#include "goby/util/asio_compat.h"
#include "goby/util/debug_logger/flex_ostream.h"    // for operator<<, Fle...
#include "goby/util/debug_logger/flex_ostreambuf.h" // for WARN, logger

#include "tcp_client.h"

goby::util::TCPClient::TCPClient(std::string server, unsigned port,
                                 const std::string& delimiter /*= "\r\n"*/,
                                 int retry_interval /*=  10*/)
    : LineBasedClient<boost::asio::ip::tcp::socket>(delimiter, retry_interval),
      socket_(io_),
      server_(std::move(server)),
      port_(as<std::string>(port))
{
}

bool goby::util::TCPClient::start_specific()
{
    using namespace goby::util::logger;
    using goby::glog;

    boost::asio::ip::tcp::resolver resolver(io_);
    boost::asio::ip::tcp::resolver::query query(
        server_, port_, boost::asio::ip::resolver_query_base::numeric_service);

    boost::system::error_code resolver_error = boost::asio::error::host_not_found;
    boost::asio::ip::tcp::resolver::iterator endpoint_iterator =
        resolver.resolve(query, resolver_error);
    if (resolver_error)
    {
        glog.is(WARN) && glog << "Error resolving address: " << server_ << ":" << port_ << ": "
                              << resolver_error.message() << std::endl;
        return false;
    }

    boost::asio::ip::tcp::resolver::iterator end;

    boost::system::error_code error = boost::asio::error::host_not_found;
    while (error && endpoint_iterator != end)
    {
        socket_.close();
        socket_.connect(*endpoint_iterator++, error);
    }

    if (error)
        glog.is(WARN) && glog << "Error connecting to address: " << server_ << ":" << port_ << ": "
                              << error.message() << std::endl;

    return error ? false : true;
}
