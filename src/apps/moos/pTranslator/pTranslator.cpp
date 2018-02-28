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

#include <dlfcn.h>

#include "goby/moos/moos_string.h"

#include "pTranslator.h"


using goby::glog;
using namespace goby::common::logger;
using goby::moos::operator<<;


pTranslatorConfig CpTranslator::cfg_;
CpTranslator* CpTranslator::inst_ = 0;

CpTranslator* CpTranslator::get_instance()
{
    if(!inst_)
        inst_ = new CpTranslator();
    return inst_;
}

void CpTranslator::delete_instance()
{
    delete inst_;
}

CpTranslator::CpTranslator()
    : GobyMOOSApp(&cfg_),
      translator_(cfg_.translator_entry(),
                  cfg_.common().lat_origin(),
                  cfg_.common().lon_origin(),
                  cfg_.modem_id_lookup_path()),
      work_(timer_io_service_)
{

    goby::util::DynamicProtobufManager::enable_compilation();

    // load all shared libraries
    for(int i = 0, n = cfg_.load_shared_library_size(); i < n; ++i)
    {
        glog.is(VERBOSE) &&
            glog << "Loading shared library: " << cfg_.load_shared_library(i) << std::endl;
        
        void* handle = dlopen(cfg_.load_shared_library(i).c_str(), RTLD_LAZY);
        if(!handle)
        {
            glog << die << "Failed ... check path provided or add to /etc/ld.so.conf "
                 << "or LD_LIBRARY_PATH" << std::endl;
        }
    }
    
    
    // load all .proto files
    for(int i = 0, n = cfg_.load_proto_file_size(); i < n; ++i)
    {
        glog.is(VERBOSE) &&
            glog << "Loading protobuf file: " << cfg_.load_proto_file(i) << std::endl;

        
        if(!goby::util::DynamicProtobufManager::find_descriptor(
               cfg_.load_proto_file(i)))
            glog.is(DIE) && glog << "Failed to load file." << std::endl;
    }

    // process translator entries
    for(int i = 0, n = cfg_.translator_entry_size(); i < n; ++i)
    {
        typedef boost::shared_ptr<google::protobuf::Message> GoogleProtobufMessagePointer;
        glog.is(VERBOSE) &&
            glog << "Checking translator entry: " << cfg_.translator_entry(i).DebugString()
                 << std::flush;

        // check that the protobuf file is loaded somehow
        goby::util::DynamicProtobufManager::new_protobuf_message<GoogleProtobufMessagePointer>(
            cfg_.translator_entry(i).protobuf_name());

        if(cfg_.translator_entry(i).trigger().type() ==
           goby::moos::protobuf::TranslatorEntry::Trigger::TRIGGER_PUBLISH)
        {
            // subscribe for trigger publish variables
            GobyMOOSApp::subscribe(cfg_.translator_entry(i).trigger().moos_var(),
                                  boost::bind(&CpTranslator::create_on_publish,
                                              this, _1, cfg_.translator_entry(i)));
        }
        else if(cfg_.translator_entry(i).trigger().type() ==
                goby::moos::protobuf::TranslatorEntry::Trigger::TRIGGER_TIME)
        {
            timers_.push_back(boost::shared_ptr<Timer>(
                                  new Timer(timer_io_service_)));

            Timer& new_timer = *timers_.back();
            
            new_timer.expires_from_now(boost::posix_time::seconds(
                                           cfg_.translator_entry(i).trigger().period()));
            // Start an asynchronous wait.
            new_timer.async_wait(boost::bind(&CpTranslator::create_on_timer, this,
                                             _1, cfg_.translator_entry(i), &new_timer));
        }

        for(int j = 0, m = cfg_.translator_entry(i).create_size(); j < m; ++j)
        {
            // subscribe for all create variables
            subscribe(cfg_.translator_entry(i).create(j).moos_var());
        }
    }


    for(int i = 0, m = cfg_.multiplex_create_moos_var_size(); i < m; ++i)
    {
        GobyMOOSApp::subscribe(cfg_.multiplex_create_moos_var(i),
                              &CpTranslator::create_on_multiplex_publish, this);
    }
    
}

CpTranslator::~CpTranslator()
{


}



void CpTranslator::loop()
{
    timer_io_service_.poll();
}


void CpTranslator::create_on_publish(const CMOOSMsg& trigger_msg,
                                     const goby::moos::protobuf::TranslatorEntry& entry)
{
    glog.is(VERBOSE) && glog << "Received trigger: " << trigger_msg << std::endl;

    if(!entry.trigger().has_mandatory_content() ||
       trigger_msg.GetString().find(entry.trigger().mandatory_content()) != std::string::npos)
        do_translation(entry);
    else
        glog.is(VERBOSE) && glog << "Message missing mandatory content for: " << entry.protobuf_name() << std::endl;
        
}

void CpTranslator::create_on_multiplex_publish(const CMOOSMsg& moos_msg)
{
    boost::shared_ptr<google::protobuf::Message> msg =
        dynamic_parse_for_moos(moos_msg.GetString());

    if(!msg)
    {
        glog.is(WARN) &&
	  glog <<  "Multiplex receive failed: Unknown Protobuf type for " << moos_msg.GetString() << "; be sure it is compiled in or directly loaded into the goby::util::DynamicProtobufManager." << std::endl;
        return;
    }

    std::multimap<std::string, CMOOSMsg> out;    

    try
    {
        out = translator_.protobuf_to_inverse_moos(*msg);
    
        for(std::multimap<std::string, CMOOSMsg>::iterator it = out.begin(), n = out.end();
            it != n; ++it)
        {
            glog.is(VERBOSE) && glog << "Inverse Publishing: " << it->second.GetKey() << std::endl;
            publish(it->second);
        }
    }
    catch(std::exception &e)
    {
        glog.is(WARN) && glog << "Failed to inverse publish: " << e.what() << std::endl;
    }
}



void CpTranslator::create_on_timer(const boost::system::error_code& error,
                                   const goby::moos::protobuf::TranslatorEntry& entry,
                                   Timer* timer)
{
  if (!error)
  {
      double skew_seconds = std::abs(goby::common::goby_time<double>() - goby::util::as<double>(timer->expires_at()));
      if( skew_seconds > ALLOWED_TIMER_SKEW_SECONDS)
      {
          glog.is(VERBOSE) && glog << "clock skew of " << skew_seconds <<  " seconds detected, resetting timer." << std::endl;
          timer->expires_at(goby::common::goby_time() + boost::posix_time::seconds(boost::posix_time::seconds(entry.trigger().period())));
      }
      else
      {
          // reset the timer
          timer->expires_at(timer->expires_at() + boost::posix_time::seconds(entry.trigger().period()));
      }
      
      
      timer->async_wait(boost::bind(&CpTranslator::create_on_timer, this,
                                       _1, entry, timer));
      
      glog.is(VERBOSE) && glog << "Received trigger for: " << entry.protobuf_name() << std::endl;
      glog.is(VERBOSE) && glog << "Next expiry: " << timer->expires_at() << std::endl;      
      
      do_translation(entry);
  }
}

void CpTranslator::do_translation(const goby::moos::protobuf::TranslatorEntry& entry)
{
    boost::shared_ptr<google::protobuf::Message> created_message =
        translator_.moos_to_protobuf<boost::shared_ptr<google::protobuf::Message> >(
            dynamic_vars().all(), entry.protobuf_name());

    glog.is(DEBUG1) &&
        glog << "Created message: \n" << created_message->DebugString() << std::endl;

    do_publish(created_message);
}

void CpTranslator::do_publish(boost::shared_ptr<google::protobuf::Message> created_message)   
{
    std::multimap<std::string, CMOOSMsg> out;    

    out = translator_.protobuf_to_moos(*created_message);
    
    for(std::multimap<std::string, CMOOSMsg>::iterator it = out.begin(), n = out.end();
        it != n; ++it)
    {
        glog.is(VERBOSE) && glog << "Publishing: " << it->second << std::endl;
        publish(it->second);
    }
}

