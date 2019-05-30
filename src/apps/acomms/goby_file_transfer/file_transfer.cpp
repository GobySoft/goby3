// Copyright 2009-2018 Toby Schneider (http://gobysoft.org/index.wt/people/toby)
//                     GobySoft, LLC (2013-)
//                     Massachusetts Institute of Technology (2007-2014)
//
//
// This file is part of the Goby Underwater Autonomy Project Binaries
// ("The Goby Binaries").
//
// The Goby Binaries are free software: you can redistribute them and/or modify
// them under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 2 of the License, or
// (at your option) any later version.
//
// The Goby Binaries are distributed in the hope that they will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with Goby.  If not, see <http://www.gnu.org/licenses/>.

#include <fstream>
#include <iostream>

#include "goby/common/zeromq_service.h"
#include "goby/util/debug_logger.h"
#include "goby/util/debug_logger/term_color.h"

#include "goby/pb/application.h"

#include "file_transfer_config.pb.h"
#include "goby/acomms/protobuf/file_transfer.pb.h"

using namespace goby::common::logger;
using goby::glog;

namespace goby
{
namespace acomms
{
class FileTransfer : public goby::pb::Application
{
  public:
    FileTransfer(protobuf::FileTransferConfig* cfg_);
    ~FileTransfer();

  private:
    void loop() {}

    void push_file();
    void pull_file();

    int send_file(const std::string& path);

    void handle_remote_transfer_request(const protobuf::TransferRequest& request);
    void handle_receive_fragment(const protobuf::FileFragment& fragment);

    void handle_receive_response(const protobuf::TransferResponse& response);

    void handle_ack(const protobuf::TransferRequest& request)
    {
        std::cout << "Got ack for request: " << request.DebugString() << std::endl;
        waiting_for_request_ack_ = false;
    }

  private:
    protobuf::FileTransferConfig& cfg_;

    enum
    {
        MAX_FILE_TRANSFER_BYTES = 1024 * 1024
    };

    typedef int ModemId;

    std::map<ModemId, std::map<int, protobuf::FileFragment> > receive_files_;
    std::map<ModemId, protobuf::TransferRequest> requests_;
    bool waiting_for_request_ack_;
};
} // namespace acomms
} // namespace goby

int main(int argc, char* argv[])
{
    goby::acomms::protobuf::FileTransferConfig cfg;
    goby::run<goby::acomms::FileTransfer>(argc, argv, &cfg);
}

using goby::glog;

goby::acomms::FileTransfer::FileTransfer(protobuf::FileTransferConfig* cfg)
    : Application(cfg), cfg_(*cfg), waiting_for_request_ack_(false)
{
    glog.is(DEBUG1) && glog << cfg_.DebugString() << std::endl;

    if (cfg_.action() != protobuf::FileTransferConfig::WAIT)
    {
        if (!cfg_.has_remote_id())
        {
            glog.is(WARN) && glog << "Must set remote_id modem ID for file destination."
                                  << std::endl;
            exit(EXIT_FAILURE);
        }
        if (!cfg_.has_local_file())
        {
            glog.is(WARN) && glog << "Must set local_file path." << std::endl;
            exit(EXIT_FAILURE);
        }
        if (!cfg_.has_remote_id())
        {
            glog.is(WARN) && glog << "Must set remote_file path." << std::endl;
            exit(EXIT_FAILURE);
        }

        const unsigned max_path = protobuf::TransferRequest::descriptor()
                                      ->FindFieldByName("file")
                                      ->options()
                                      .GetExtension(dccl::field)
                                      .max_length();
        if (cfg_.remote_file().size() > max_path)
        {
            glog.is(WARN) && glog << "remote_file full path must be less than " << max_path
                                  << " characters." << std::endl;
            exit(EXIT_FAILURE);
        }
    }

    subscribe(&FileTransfer::handle_ack, this,
              "QueueAckOrig" + goby::util::as<std::string>(cfg_.local_id()));

    subscribe(&FileTransfer::handle_remote_transfer_request, this,
              "QueueRx" + goby::util::as<std::string>(cfg_.local_id()));

    subscribe(&FileTransfer::handle_receive_fragment, this,
              "QueueRx" + goby::util::as<std::string>(cfg_.local_id()));

    subscribe(&FileTransfer::handle_receive_response, this,
              "QueueRx" + goby::util::as<std::string>(cfg_.local_id()));

    try
    {
        if (cfg_.action() == protobuf::FileTransferConfig::PUSH)
            push_file();
        else if (cfg_.action() == protobuf::FileTransferConfig::PULL)
            pull_file();
    }
    catch (protobuf::TransferResponse::ErrorCode& c)
    {
        glog.is(WARN) && glog << "File transfer action failed: "
                              << protobuf::TransferResponse::ErrorCode_Name(c) << std::endl;
        if (!cfg_.daemon())
            exit(EXIT_FAILURE);
    }
    catch (std::exception& e)
    {
        glog.is(WARN) && glog << "File transfer action failed: " << e.what() << std::endl;
        if (!cfg_.daemon())
            exit(EXIT_FAILURE);
    }
}

void goby::acomms::FileTransfer::push_file()
{
    protobuf::TransferRequest request;
    request.set_src(cfg_.local_id());
    request.set_dest(cfg_.remote_id());
    request.set_push_or_pull(protobuf::TransferRequest::PUSH);
    request.set_file(cfg_.remote_file());

    publish(request, "QueuePush" + goby::util::as<std::string>(cfg_.local_id()));

    double start_time = goby::common::goby_time<double>();
    waiting_for_request_ack_ = true;
    while (goby::common::goby_time<double>() < start_time + cfg_.request_timeout())
    {
        zeromq_service().poll(10000);
        if (!waiting_for_request_ack_)
        {
            send_file(cfg_.local_file());
            break;
        }
    }
}

void goby::acomms::FileTransfer::pull_file()
{
    protobuf::TransferRequest request;
    request.set_src(cfg_.local_id());
    request.set_dest(cfg_.remote_id());
    request.set_push_or_pull(protobuf::TransferRequest::PULL);
    request.set_file(cfg_.remote_file());

    publish(request, "QueuePush" + goby::util::as<std::string>(cfg_.local_id()));

    // set up local request for receiving and writing
    request.set_file(cfg_.local_file());
    request.set_src(cfg_.remote_id());
    request.set_dest(cfg_.local_id());
    receive_files_[request.src()].clear();
    requests_[request.src()] = request;
}

int goby::acomms::FileTransfer::send_file(const std::string& path)
{
    std::ifstream send_file(path.c_str(), std::ios::binary | std::ios::ate);

    glog.is(VERBOSE) && glog << "Attempting to transfer: " << path << std::endl;

    // check open
    if (!send_file.is_open())
        throw protobuf::TransferResponse::COULD_NOT_READ_FILE;

    // check size
    std::streampos size = send_file.tellg();
    glog.is(VERBOSE) && glog << "File size: " << size << std::endl;

    if (size > MAX_FILE_TRANSFER_BYTES)
    {
        glog.is(WARN) && glog << "File exceeds maximum supported size of "
                              << MAX_FILE_TRANSFER_BYTES << "B" << std::endl;
        throw protobuf::TransferResponse::FILE_TOO_LARGE;
    }

    // seek to front
    send_file.seekg(0, send_file.beg);

    int size_acquired = 0;
    // fragment into little bits

    int fragment_size = protobuf::FileFragment::descriptor()
                            ->FindFieldByName("data")
                            ->options()
                            .GetExtension(dccl::field)
                            .max_length();

    protobuf::FileFragment reference_fragment;
    reference_fragment.set_src(cfg_.local_id());
    reference_fragment.set_dest(cfg_.remote_id());

    std::vector<protobuf::FileFragment> fragments(std::ceil((double)size / fragment_size),
                                                  reference_fragment);

    std::vector<protobuf::FileFragment>::iterator fragments_it = fragments.begin();
    std::vector<char> buffer(fragment_size);
    int fragment_idx = 0;
    while (send_file.good())
    {
        protobuf::FileFragment& fragment = *(fragments_it++);
        send_file.read(&buffer[0], fragment_size);
        int bytes_read = send_file.gcount();
        size_acquired += bytes_read;
        fragment.set_fragment(fragment_idx++);
        if (size_acquired == size)
            fragment.set_is_last_fragment(true);
        else
            fragment.set_is_last_fragment(false);
        fragment.set_num_bytes(bytes_read);

        fragment.set_data(std::string(buffer.begin(), buffer.begin() + bytes_read));
    }

    if (!send_file.eof())
        throw protobuf::TransferResponse::ERROR_WHILE_READING;

    // FOR TESTING!
    // fragments.resize(fragments.size()*2);
    // std::copy_backward(fragments.begin(), fragments.begin()+fragments.size()/2, fragments.end());
    // std::random_shuffle(fragments.begin(), fragments.end());
    for (int i = 0, n = fragments.size(); i < n; ++i)
    {
        glog.is(VERBOSE) && glog << fragments[i].ShortDebugString() << std::endl;
        publish(fragments[i], "QueuePush" + goby::util::as<std::string>(cfg_.local_id()));
    }

    return fragment_idx;
}

goby::acomms::FileTransfer::~FileTransfer() {}

void goby::acomms::FileTransfer::handle_remote_transfer_request(
    const protobuf::TransferRequest& request)
{
    glog.is(VERBOSE) && glog << "Received remote transfer request: " << request.DebugString()
                             << std::endl;

    if (request.push_or_pull() == protobuf::TransferRequest::PUSH)
    {
        glog.is(VERBOSE) && glog << "Preparing to receive file..." << std::endl;
        receive_files_[request.src()].clear();
    }
    else if (request.push_or_pull() == protobuf::TransferRequest::PULL)
    {
        protobuf::TransferResponse response;
        response.set_src(request.dest());
        response.set_dest(request.src());
        try
        {
            response.set_num_fragments(send_file(request.file()));
            response.set_transfer_successful(true);
        }
        catch (protobuf::TransferResponse::ErrorCode& c)
        {
            glog.is(WARN) && glog << "File transfer action failed: "
                                  << protobuf::TransferResponse::ErrorCode_Name(c) << std::endl;
            response.set_transfer_successful(false);
            response.set_error(c);
            if (!cfg_.daemon())
                exit(EXIT_FAILURE);
        }
        catch (std::exception& e)
        {
            glog.is(WARN) && glog << "File transfer action failed: " << e.what() << std::endl;
            if (!cfg_.daemon())
                exit(EXIT_FAILURE);

            response.set_transfer_successful(false);
            response.set_error(protobuf::TransferResponse::OTHER_ERROR);
        }
        publish(response, "QueuePush" + goby::util::as<std::string>(cfg_.local_id()));
    }
    requests_[request.src()] = request;
}

void goby::acomms::FileTransfer::handle_receive_fragment(const protobuf::FileFragment& fragment)
{
    std::map<int, protobuf::FileFragment>& receive = receive_files_[fragment.src()];

    receive.insert(std::make_pair(fragment.fragment(), fragment));

    glog.is(VERBOSE) && glog << "Received fragment #" << fragment.fragment()
                             << ", total received: " << receive.size() << std::endl;

    if (receive.rbegin()->second.is_last_fragment())
    {
        if ((int)receive.size() == receive.rbegin()->second.fragment() + 1)
        {
            protobuf::TransferResponse response;
            response.set_src(requests_[fragment.src()].dest());
            response.set_dest(requests_[fragment.src()].src());

            try
            {
                glog.is(VERBOSE) && glog << "Received all fragments!" << std::endl;
                glog.is(VERBOSE) && glog << "Writing to " << requests_[fragment.src()].file()
                                         << std::endl;
                std::ofstream receive_file(requests_[fragment.src()].file().c_str(),
                                           std::ios::binary);

                // check open
                if (!receive_file.is_open())
                    throw(protobuf::TransferResponse::COULD_NOT_WRITE_FILE);

                for (std::map<int, protobuf::FileFragment>::const_iterator it = receive.begin(),
                                                                           end = receive.end();
                     it != end; ++it)
                { receive_file.write(it->second.data().c_str(), it->second.num_bytes()); }

                receive_file.close();
                response.set_transfer_successful(true);
                if (!cfg_.daemon())
                    exit(EXIT_SUCCESS);
            }
            catch (protobuf::TransferResponse::ErrorCode& c)
            {
                glog.is(WARN) && glog << "File transfer action failed: "
                                      << protobuf::TransferResponse::ErrorCode_Name(c) << std::endl;
                response.set_transfer_successful(false);
                response.set_error(c);

                if (!cfg_.daemon())
                    exit(EXIT_FAILURE);
            }
            catch (std::exception& e)
            {
                glog.is(WARN) && glog << "File transfer action failed: " << e.what() << std::endl;
                if (!cfg_.daemon())
                    exit(EXIT_FAILURE);

                response.set_transfer_successful(false);
                response.set_error(protobuf::TransferResponse::OTHER_ERROR);
            }
            publish(response, "QueuePush" + goby::util::as<std::string>(cfg_.local_id()));
        }
        else
        {
            glog.is(VERBOSE) && glog << "Still waiting on some fragments..." << std::endl;
        }
    }
}

void goby::acomms::FileTransfer::handle_receive_response(const protobuf::TransferResponse& response)
{
    glog.is(VERBOSE) && glog << "Received response for file transfer: " << response.DebugString()
                             << std::flush;

    if (!response.transfer_successful())
        glog.is(WARN) && glog << "Transfer failed: "
                              << protobuf::TransferResponse::ErrorCode_Name(response.error())
                              << std::endl;

    if (!cfg_.daemon())
    {
        if (response.transfer_successful())
        {
            glog.is(VERBOSE) && glog << "File transfer completed successfully." << std::endl;
        }
        else
        {
            exit(EXIT_FAILURE);
        }
    }
}
