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

#include "goby/middleware/marshalling/protobuf.h"

#include "goby/acomms/protobuf/file_transfer.pb.h"
#include "goby/middleware/acomms/groups.h"
#include "goby/zeromq/application/single_thread.h"
#include "goby/zeromq/protobuf/file_transfer_config.pb.h"

using goby::glog;
using namespace goby::util::logger;

namespace goby
{
namespace apps
{
namespace zeromq
{
namespace acomms
{
class FileTransfer : public goby::zeromq::SingleThreadApplication<protobuf::FileTransferConfig>
{
  public:
    FileTransfer();
    ~FileTransfer();

  private:
    void push_file();
    void pull_file();

    int send_file(const std::string& path);

    void handle_remote_transfer_request(const goby::acomms::protobuf::TransferRequest& request);
    void handle_receive_fragment(const goby::acomms::protobuf::FileFragment& fragment);

    void handle_receive_response(const goby::acomms::protobuf::TransferResponse& response);

    void handle_ack(const goby::acomms::protobuf::TransferRequest& request)
    {
        std::cout << "Got ack for request: " << request.DebugString() << std::endl;
        waiting_for_request_ack_ = false;
    }

  private:
    enum
    {
        MAX_FILE_TRANSFER_BYTES = 1024 * 1024
    };

    typedef int ModemId;

    std::map<ModemId, std::map<int, goby::acomms::protobuf::FileFragment>> receive_files_;
    std::map<ModemId, goby::acomms::protobuf::TransferRequest> requests_;
    bool waiting_for_request_ack_;

    goby::middleware::DynamicGroup queue_rx_group_;
    goby::middleware::DynamicGroup queue_ack_orig_group_;
    goby::middleware::DynamicGroup queue_push_group_;
};
} // namespace acomms
} // namespace zeromq
} // namespace apps
} // namespace goby

int main(int argc, char* argv[])
{
    goby::run<goby::apps::zeromq::acomms::FileTransfer>(argc, argv);
}

using goby::glog;

goby::apps::zeromq::acomms::FileTransfer::FileTransfer()
    : waiting_for_request_ack_(false),
      queue_rx_group_(goby::middleware::acomms::groups::queue_rx, cfg().local_id()),
      queue_ack_orig_group_(goby::middleware::acomms::groups::queue_ack_orig, cfg().local_id()),
      queue_push_group_(goby::middleware::acomms::groups::queue_push, cfg().local_id())
{
    if (cfg().action() != protobuf::FileTransferConfig::WAIT)
    {
        if (!cfg().has_remote_id())
        {
            glog.is(WARN) && glog << "Must set remote_id modem ID for file destination."
                                  << std::endl;
            exit(EXIT_FAILURE);
        }
        if (!cfg().has_local_file())
        {
            glog.is(WARN) && glog << "Must set local_file path." << std::endl;
            exit(EXIT_FAILURE);
        }
        if (!cfg().has_remote_id())
        {
            glog.is(WARN) && glog << "Must set remote_file path." << std::endl;
            exit(EXIT_FAILURE);
        }

        const unsigned max_path = goby::acomms::protobuf::TransferRequest::descriptor()
                                      ->FindFieldByName("file")
                                      ->options()
                                      .GetExtension(dccl::field)
                                      .max_length();
        if (cfg().remote_file().size() > max_path)
        {
            glog.is(WARN) && glog << "remote_file full path must be less than " << max_path
                                  << " characters." << std::endl;
            exit(EXIT_FAILURE);
        }
    }

    interprocess().subscribe_dynamic<goby::acomms::protobuf::TransferRequest>(
        std::bind(&FileTransfer::handle_ack, this, std::placeholders::_1), queue_ack_orig_group_);

    interprocess().subscribe_dynamic<goby::acomms::protobuf::TransferRequest>(
        std::bind(&FileTransfer::handle_remote_transfer_request, this, std::placeholders::_1),
        queue_rx_group_);

    interprocess().subscribe_dynamic<goby::acomms::protobuf::FileFragment>(
        std::bind(&FileTransfer::handle_receive_fragment, this, std::placeholders::_1),
        queue_rx_group_);

    interprocess().subscribe_dynamic<goby::acomms::protobuf::TransferResponse>(
        std::bind(&FileTransfer::handle_receive_response, this, std::placeholders::_1),
        queue_rx_group_);

    try
    {
        if (cfg().action() == protobuf::FileTransferConfig::PUSH)
            push_file();
        else if (cfg().action() == protobuf::FileTransferConfig::PULL)
            pull_file();
    }
    catch (goby::acomms::protobuf::TransferResponse::ErrorCode& c)
    {
        glog.is(WARN) && glog << "File transfer action failed: "
                              << goby::acomms::protobuf::TransferResponse::ErrorCode_Name(c)
                              << std::endl;
        if (!cfg().daemon())
            exit(EXIT_FAILURE);
    }
    catch (std::exception& e)
    {
        glog.is(WARN) && glog << "File transfer action failed: " << e.what() << std::endl;
        if (!cfg().daemon())
            exit(EXIT_FAILURE);
    }
}

void goby::apps::zeromq::acomms::FileTransfer::push_file()
{
    goby::acomms::protobuf::TransferRequest request;
    request.set_src(cfg().local_id());
    request.set_dest(cfg().remote_id());
    request.set_push_or_pull(goby::acomms::protobuf::TransferRequest::PUSH);
    request.set_file(cfg().remote_file());

    interprocess().publish_dynamic(request, queue_push_group_);

    double start_time = goby::time::SystemClock::now<goby::time::SITime>().value();
    waiting_for_request_ack_ = true;
    while (goby::time::SystemClock::now<goby::time::SITime>().value() <
           start_time + cfg().request_timeout())
    {
        interprocess().poll(std::chrono::milliseconds(10));
        if (!waiting_for_request_ack_)
        {
            send_file(cfg().local_file());
            break;
        }
    }
}

void goby::apps::zeromq::acomms::FileTransfer::pull_file()
{
    goby::acomms::protobuf::TransferRequest request;
    request.set_src(cfg().local_id());
    request.set_dest(cfg().remote_id());
    request.set_push_or_pull(goby::acomms::protobuf::TransferRequest::PULL);
    request.set_file(cfg().remote_file());

    interprocess().publish_dynamic(request, queue_push_group_);

    // set up local request for receiving and writing
    request.set_file(cfg().local_file());
    request.set_src(cfg().remote_id());
    request.set_dest(cfg().local_id());
    receive_files_[request.src()].clear();
    requests_[request.src()] = request;
}

int goby::apps::zeromq::acomms::FileTransfer::send_file(const std::string& path)
{
    std::ifstream send_file(path.c_str(), std::ios::binary | std::ios::ate);

    glog.is(VERBOSE) && glog << "Attempting to transfer: " << path << std::endl;

    // check open
    if (!send_file.is_open())
        throw goby::acomms::protobuf::TransferResponse::COULD_NOT_READ_FILE;

    // check size
    std::streampos size = send_file.tellg();
    glog.is(VERBOSE) && glog << "File size: " << size << std::endl;

    if (size > MAX_FILE_TRANSFER_BYTES)
    {
        glog.is(WARN) && glog << "File exceeds maximum supported size of "
                              << MAX_FILE_TRANSFER_BYTES << "B" << std::endl;
        throw goby::acomms::protobuf::TransferResponse::FILE_TOO_LARGE;
    }

    // seek to front
    send_file.seekg(0, send_file.beg);

    int size_acquired = 0;
    // fragment into little bits

    int fragment_size = goby::acomms::protobuf::FileFragment::descriptor()
                            ->FindFieldByName("data")
                            ->options()
                            .GetExtension(dccl::field)
                            .max_length();

    goby::acomms::protobuf::FileFragment reference_fragment;
    reference_fragment.set_src(cfg().local_id());
    reference_fragment.set_dest(cfg().remote_id());

    std::vector<goby::acomms::protobuf::FileFragment> fragments(
        std::ceil((double)size / fragment_size), reference_fragment);

    std::vector<goby::acomms::protobuf::FileFragment>::iterator fragments_it = fragments.begin();
    std::vector<char> buffer(fragment_size);
    int fragment_idx = 0;
    while (send_file.good())
    {
        goby::acomms::protobuf::FileFragment& fragment = *(fragments_it++);
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
        throw goby::acomms::protobuf::TransferResponse::ERROR_WHILE_READING;

    // FOR TESTING!
    // fragments.resize(fragments.size()*2);
    // std::copy_backward(fragments.begin(), fragments.begin()+fragments.size()/2, fragments.end());
    // std::random_shuffle(fragments.begin(), fragments.end());
    for (int i = 0, n = fragments.size(); i < n; ++i)
    {
        glog.is(VERBOSE) && glog << fragments[i].ShortDebugString() << std::endl;
        interprocess().publish_dynamic(fragments[i], queue_push_group_);
    }

    return fragment_idx;
}

goby::apps::zeromq::acomms::FileTransfer::~FileTransfer() {}

void goby::apps::zeromq::acomms::FileTransfer::handle_remote_transfer_request(
    const goby::acomms::protobuf::TransferRequest& request)
{
    glog.is(VERBOSE) && glog << "Received remote transfer request: " << request.DebugString()
                             << std::endl;

    if (request.push_or_pull() == goby::acomms::protobuf::TransferRequest::PUSH)
    {
        glog.is(VERBOSE) && glog << "Preparing to receive file..." << std::endl;
        receive_files_[request.src()].clear();
    }
    else if (request.push_or_pull() == goby::acomms::protobuf::TransferRequest::PULL)
    {
        goby::acomms::protobuf::TransferResponse response;
        response.set_src(request.dest());
        response.set_dest(request.src());
        try
        {
            response.set_num_fragments(send_file(request.file()));
            response.set_transfer_successful(true);
        }
        catch (goby::acomms::protobuf::TransferResponse::ErrorCode& c)
        {
            glog.is(WARN) && glog << "File transfer action failed: "
                                  << goby::acomms::protobuf::TransferResponse::ErrorCode_Name(c)
                                  << std::endl;
            response.set_transfer_successful(false);
            response.set_error(c);
            if (!cfg().daemon())
                exit(EXIT_FAILURE);
        }
        catch (std::exception& e)
        {
            glog.is(WARN) && glog << "File transfer action failed: " << e.what() << std::endl;
            if (!cfg().daemon())
                exit(EXIT_FAILURE);

            response.set_transfer_successful(false);
            response.set_error(goby::acomms::protobuf::TransferResponse::OTHER_ERROR);
        }
        interprocess().publish_dynamic(response, queue_push_group_);
    }
    requests_[request.src()] = request;
}

void goby::apps::zeromq::acomms::FileTransfer::handle_receive_fragment(
    const goby::acomms::protobuf::FileFragment& fragment)
{
    std::map<int, goby::acomms::protobuf::FileFragment>& receive = receive_files_[fragment.src()];

    receive.insert(std::make_pair(fragment.fragment(), fragment));

    glog.is(VERBOSE) && glog << "Received fragment #" << fragment.fragment()
                             << ", total received: " << receive.size() << std::endl;

    if (receive.rbegin()->second.is_last_fragment())
    {
        if ((int)receive.size() == receive.rbegin()->second.fragment() + 1)
        {
            goby::acomms::protobuf::TransferResponse response;
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
                    throw(goby::acomms::protobuf::TransferResponse::COULD_NOT_WRITE_FILE);

                for (std::map<int, goby::acomms::protobuf::FileFragment>::const_iterator
                         it = receive.begin(),
                         end = receive.end();
                     it != end; ++it)
                {
                    receive_file.write(it->second.data().c_str(), it->second.num_bytes());
                }

                receive_file.close();
                response.set_transfer_successful(true);
                if (!cfg().daemon())
                    exit(EXIT_SUCCESS);
            }
            catch (goby::acomms::protobuf::TransferResponse::ErrorCode& c)
            {
                glog.is(WARN) && glog << "File transfer action failed: "
                                      << goby::acomms::protobuf::TransferResponse::ErrorCode_Name(c)
                                      << std::endl;
                response.set_transfer_successful(false);
                response.set_error(c);

                if (!cfg().daemon())
                    exit(EXIT_FAILURE);
            }
            catch (std::exception& e)
            {
                glog.is(WARN) && glog << "File transfer action failed: " << e.what() << std::endl;
                if (!cfg().daemon())
                    exit(EXIT_FAILURE);

                response.set_transfer_successful(false);
                response.set_error(goby::acomms::protobuf::TransferResponse::OTHER_ERROR);
            }
            interprocess().publish_dynamic(response, queue_push_group_);
        }
        else
        {
            glog.is(VERBOSE) && glog << "Still waiting on some fragments..." << std::endl;
        }
    }
}

void goby::apps::zeromq::acomms::FileTransfer::handle_receive_response(
    const goby::acomms::protobuf::TransferResponse& response)
{
    glog.is(VERBOSE) && glog << "Received response for file transfer: " << response.DebugString()
                             << std::flush;

    if (!response.transfer_successful())
        glog.is(WARN) &&
            glog << "Transfer failed: "
                 << goby::acomms::protobuf::TransferResponse::ErrorCode_Name(response.error())
                 << std::endl;

    if (!cfg().daemon())
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
