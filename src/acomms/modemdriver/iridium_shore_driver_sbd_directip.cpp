// Copyright 2015-2023:
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

#include "goby/acomms/modemdriver/iridium_shore_sbd.h"    // for SBDMO...
#include "goby/acomms/protobuf/iridium_sbd_directip.pb.h" // for Direc...
#include "goby/util/as.h"                                 // for as
#include "goby/util/debug_logger/flex_ostream.h"          // for opera...
#include "goby/util/debug_logger/flex_ostreambuf.h"       // for DEBUG1
#include "goby/util/debug_logger/logger_manipulators.h"   // for opera...

#include "goby/util/thirdparty/jwt-cpp/traits/nlohmann-json/defaults.h"

#include "goby/util/thirdparty/jwt-cpp/jwt.h"

#include "iridium_shore_driver.h"

using namespace goby::util::logger;
using goby::glog;
using goby::acomms::iridium::protobuf::DirectIPMTHeader;
using goby::acomms::iridium::protobuf::DirectIPMTPayload;

std::string create_sbd_mt_data_message(const std::string& payload, const std::string& imei);

void goby::acomms::IridiumShoreDriver::startup_sbd_directip(const protobuf::DriverConfig& cfg)
{
    directip_mo_sbd_server_.reset(
        new directip::SBDServer(sbd_io_, iridium_shore_driver_cfg().mo_sbd_server_port()));
}

void goby::acomms::IridiumShoreDriver::receive_sbd_mo_directip()
{
    try
    {
        sbd_io_.poll();
    }
    catch (std::exception& e)
    {
        glog.is(DEBUG1) && glog << warn << group(glog_in_group())
                                << "Could not handle SBD receive: " << e.what() << std::endl;
    }

    auto it = directip_mo_sbd_server_->connections().begin(),
         end = directip_mo_sbd_server_->connections().end();
    while (it != end)
    {
        const int timeout = 5;
        if ((*it)->message().data_ready())
        {
            glog.is(DEBUG1) && glog << group(glog_in_group()) << "Rx SBD PreHeader: "
                                    << (*it)->message().pre_header().DebugString() << std::endl;
            glog.is(DEBUG1) && glog << group(glog_in_group())
                                    << "Rx SBD Header: " << (*it)->message().header().DebugString()
                                    << std::endl;
            glog.is(DEBUG1) && glog << group(glog_in_group())
                                    << "Rx SBD Payload: " << (*it)->message().body().DebugString()
                                    << std::endl;

            receive_sbd_mo_data((*it)->message().body().payload());
            directip_mo_sbd_server_->connections().erase(it++);
        }
        else if ((*it)->connect_time() > 0 &&
                 (time::SystemClock::now().time_since_epoch() / std::chrono::seconds(1) >
                  ((*it)->connect_time() + timeout)))
        {
            glog.is(DEBUG1) && glog << group(glog_in_group())
                                    << "Removing SBD connection that has timed out:"
                                    << (*it)->remote_endpoint_str() << std::endl;
            directip_mo_sbd_server_->connections().erase(it++);
        }
        else
        {
            ++it;
        }
    }
}

void goby::acomms::IridiumShoreDriver::send_sbd_mt_directip(const std::string& bytes,
                                                            const std::string& imei)
{
    try
    {
        using boost::asio::ip::tcp;

        boost::asio::io_service io_service;

        tcp::resolver resolver(io_service);
        tcp::resolver::query query(
            iridium_shore_driver_cfg().mt_sbd_server_address(),
            goby::util::as<std::string>(iridium_shore_driver_cfg().mt_sbd_server_port()),
            boost::asio::ip::resolver_query_base::numeric_service);
        tcp::resolver::iterator endpoint_iterator = resolver.resolve(query);
        tcp::resolver::iterator end;

        tcp::socket socket(io_service);
        boost::system::error_code error = boost::asio::error::host_not_found;
        while (error && endpoint_iterator != end)
        {
            socket.close();
            socket.connect(*endpoint_iterator++, error);
        }
        if (error)
            throw boost::system::system_error(error);

        boost::asio::write(socket, boost::asio::buffer(create_sbd_mt_data_message(bytes, imei)));

        directip::SBDMTConfirmationMessageReader message(socket);
        boost::asio::async_read(
            socket, boost::asio::buffer(message.data()),
            boost::asio::transfer_at_least(directip::SBDMessageReader::PRE_HEADER_SIZE),
            boost::bind(&directip::SBDMessageReader::pre_header_handler, &message,
                        boost::placeholders::_1, boost::placeholders::_2));

        double start_time = time::SystemClock::now().time_since_epoch() / std::chrono::seconds(1);
        const int timeout = 5;

        while (!message.data_ready() &&
               (start_time + timeout >
                time::SystemClock::now().time_since_epoch() / std::chrono::seconds(1)))
            io_service.poll();

        if (message.data_ready())
        {
            glog.is(DEBUG1) && glog << "Tx SBD Confirmation: " << message.confirm().DebugString()
                                    << std::endl;
        }
        else
        {
            glog.is(WARN) && glog << "Timeout waiting for confirmation message from DirectIP server"
                                  << std::endl;
        }
    }
    catch (std::exception& e)
    {
        glog.is(WARN) && glog << "Could not sent MT SBD message: " << e.what() << std::endl;
    }
}

std::string create_sbd_mt_data_message(const std::string& bytes, const std::string& imei)
{
    enum
    {
        PRE_HEADER_SIZE = 3,
        BITS_PER_BYTE = 8,
        IEI_SIZE = 3,
        HEADER_SIZE = 21
    };

    enum
    {
        IEI_MT_HEADER = 0x41,
        IEI_MT_PAYLOAD = 0x42
    };

    static int i = 0;
    DirectIPMTHeader header;
    header.set_iei(IEI_MT_HEADER);
    header.set_length(HEADER_SIZE);
    header.set_client_id(i++);
    header.set_imei(imei);

    enum
    {
        DISP_FLAG_FLUSH_MT_QUEUE = 0x01,
        DISP_FLAG_SEND_RING_ALERT_NO_MTM = 0x02,
        DISP_FLAG_UPDATE_SSD_LOCATION = 0x08,
        DISP_FLAG_HIGH_PRIORITY_MESSAGE = 0x10,
        DISP_FLAG_ASSIGN_MTMSN = 0x20
    };

    header.set_disposition_flags(DISP_FLAG_FLUSH_MT_QUEUE);

    std::string header_bytes(IEI_SIZE + HEADER_SIZE, '\0');

    std::string::size_type pos = 0;
    enum
    {
        HEADER_IEI = 1,
        HEADER_LENGTH = 2,
        HEADER_CLIENT_ID = 3,
        HEADER_IMEI = 4,
        HEADER_DISPOSITION_FLAGS = 5
    };

    for (int field = HEADER_IEI; field <= HEADER_DISPOSITION_FLAGS; ++field)
    {
        switch (field)
        {
            case HEADER_IEI: header_bytes[pos++] = header.iei() & 0xff; break;

            case HEADER_LENGTH:
                header_bytes[pos++] = (header.length() >> BITS_PER_BYTE) & 0xff;
                header_bytes[pos++] = (header.length()) & 0xff;
                break;

            case HEADER_CLIENT_ID:
                header_bytes[pos++] = (header.client_id() >> 3 * BITS_PER_BYTE) & 0xff;
                header_bytes[pos++] = (header.client_id() >> 2 * BITS_PER_BYTE) & 0xff;
                header_bytes[pos++] = (header.client_id() >> BITS_PER_BYTE) & 0xff;
                header_bytes[pos++] = (header.client_id()) & 0xff;
                break;

            case HEADER_IMEI:
                header_bytes.replace(pos, 15, header.imei());
                pos += 15;
                break;

            case HEADER_DISPOSITION_FLAGS:
                header_bytes[pos++] = (header.disposition_flags() >> BITS_PER_BYTE) & 0xff;
                header_bytes[pos++] = (header.disposition_flags()) & 0xff;
                break;
        }
    }

    DirectIPMTPayload payload;
    payload.set_iei(IEI_MT_PAYLOAD);
    payload.set_length(bytes.size());
    payload.set_payload(bytes);

    std::string payload_bytes(IEI_SIZE + bytes.size(), '\0');
    payload_bytes[0] = payload.iei();
    payload_bytes[1] = (payload.length() >> BITS_PER_BYTE) & 0xff;
    payload_bytes[2] = (payload.length()) & 0xff;
    payload_bytes.replace(3, payload.payload().size(), payload.payload());

    // Protocol Revision Number (1 byte) == 1
    // Overall Message Length (2 bytes)
    int overall_length = header_bytes.size() + payload_bytes.size();
    std::string pre_header_bytes(PRE_HEADER_SIZE, '\0');
    pre_header_bytes[0] = 1;
    pre_header_bytes[1] = (overall_length >> BITS_PER_BYTE) & 0xff;
    pre_header_bytes[2] = (overall_length)&0xff;

    glog.is(DEBUG1) && glog << "Tx SBD PreHeader: " << goby::util::hex_encode(pre_header_bytes)
                            << std::endl;
    glog.is(DEBUG1) && glog << "Tx SBD Header: " << header.DebugString() << std::endl;
    glog.is(DEBUG1) && glog << "Tx SBD Payload: " << payload.DebugString() << std::endl;

    return pre_header_bytes + header_bytes + payload_bytes;
}
