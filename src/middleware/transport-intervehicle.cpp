#include "transport-intervehicle.h"

using goby::glog;
using namespace goby::common::logger;


goby::ModemDriverThread::ModemDriverThread(const protobuf::InterVehiclePortalConfig& cfg, std::atomic<bool>& alive, std::shared_ptr<std::condition_variable_any> poller_cv) :
    cfg_(cfg),
    alive_(alive),
    poller_cv_(poller_cv)
{
    switch(cfg_.driver_type())
    {
        case goby::acomms::protobuf::DRIVER_WHOI_MICROMODEM:
            driver_.reset(new goby::acomms::MMDriver);
            break;

        case goby::acomms::protobuf::DRIVER_IRIDIUM:
            driver_.reset(new goby::acomms::IridiumDriver);
            break;
            
        case goby::acomms::protobuf::DRIVER_UDP:
            asio_service_.push_back(std::unique_ptr<boost::asio::io_service>(
                                        new boost::asio::io_service));
            driver_.reset(new goby::acomms::UDPDriver(asio_service_.back().get()));
            break;

        case goby::acomms::protobuf::DRIVER_IRIDIUM_SHORE:
            driver_.reset(new goby::acomms::IridiumShoreDriver);
            break;
            
        case goby::acomms::protobuf::DRIVER_NONE: break;

        default:
            throw(std::runtime_error("Unsupported driver type: " + goby::acomms::protobuf::DriverType_Name(cfg_.driver_type())));
            break;
    }
    
            
    driver_->signal_receive.connect([&](const goby::acomms::protobuf::ModemTransmission& rx_msg) { this->_receive(rx_msg); });
    driver_->signal_data_request.connect([&](goby::acomms::protobuf::ModemTransmission* msg) { this->_data_request(msg); });    

    goby::acomms::bind(mac_, *driver_);
    
    
    //q_manager_.set_cfg(cfg_.queue_cfg());
    mac_.startup(cfg_.mac_cfg());
    driver_->startup(cfg_.driver_cfg());
}

void goby::ModemDriverThread::run()
{
    while(alive_)
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            driver_->do_work();
            mac_.do_work();
            //q_manager_.do_work();
        }
        // run at ~10Hz
        usleep(100000);
    }
}

void goby::ModemDriverThread::publish(const std::string& bytes)
{
    std::lock_guard<std::mutex> lock(mutex_);
    sending_.push_back(bytes);
}

bool goby::ModemDriverThread::retrieve_message(goby::acomms::protobuf::ModemTransmission* msg)
{
    if(received_.empty())
    {
        return false;
    }
    else
    {
        std::lock_guard<std::mutex> lock(mutex_);    
        *msg = received_.front();
        received_.pop_front();
        return true;
    }
}



void goby::ModemDriverThread::_receive(const goby::acomms::protobuf::ModemTransmission& rx_msg)
{
    received_.push_back(rx_msg);
    poller_cv_->notify_all();
    glog.is(DEBUG1) && glog << "Received: " << rx_msg.ShortDebugString() << std::endl;
}        

void goby::ModemDriverThread::_data_request(goby::acomms::protobuf::ModemTransmission* msg)
{
    for(auto frame_number = msg->frame_start(),
            total_frames = msg->max_num_frames() + msg->frame_start();
        frame_number < total_frames; ++frame_number)
    {
        if(sending_.empty())
        {
            break;
        }
        else
        {
            // todo check sizes
            *msg->add_frame() = sending_.front();
            sending_.pop_front();
        }
    }
    
}

