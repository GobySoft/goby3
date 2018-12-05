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

#include "goby/common/logger.h"
#include "goby/util/as.h"

#include "tcp_client.h"

goby::util::TCPClient::TCPClient(const std::string& server, unsigned port,
                                 const std::string& delimiter /*= "\r\n"*/,
                                 int retry_interval /*=  10*/)
    : LineBasedClient<boost::asio::ip::tcp::socket>(delimiter, retry_interval),
      socket_(io_service_), server_(server), port_(as<std::string>(port))
{
}

bool goby::util::TCPClient::start_specific()
{
    using namespace goby::common::logger;
    using goby::glog;

    boost::asio::ip::tcp::resolver resolver(io_service_);
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
