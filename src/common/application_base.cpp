// Copyright 2009-2017 Toby Schneider (http://gobysoft.org/index.wt/people/toby)
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

#include <boost/format.hpp>
#include <boost/filesystem.hpp>

#include "application_base.h"
#include "goby/common/configuration_reader.h"
#include "core_helpers.h"
#include <csignal>

using goby::util::as;

using goby::glog;
using namespace goby::common::logger;

int goby::common::ApplicationBase::argc_ = 0;
char** goby::common::ApplicationBase::argv_ = 0;

goby::common::ApplicationBase::ApplicationBase(google::protobuf::Message* cfg /*= 0*/)
    : all_cfg_(cfg),
      base_cfg_(0),
      own_base_cfg_(false),
      alive_(true)
{

    //
    // read the configuration
    //
    boost::program_options::options_description od("Allowed options");
    boost::program_options::variables_map var_map;
    try
    {
        std::string application_name;
        common::ConfigReader::read_cfg(argc_, argv_, cfg, &application_name, &od, &var_map);

        // extract the AppBaseConfig assuming the user provided it in their configuration
        // .proto file
        if(cfg)
        {
            const google::protobuf::Descriptor* desc = cfg->GetDescriptor();
            for (int i = 0, n = desc->field_count(); i < n; ++i)
            {
                const google::protobuf::FieldDescriptor* field_desc = desc->field(i);
                if(field_desc->cpp_type() == google::protobuf::FieldDescriptor::CPPTYPE_MESSAGE
                   && field_desc->message_type() == ::AppBaseConfig::descriptor())
                {
                    base_cfg_ = dynamic_cast<AppBaseConfig*>(cfg->GetReflection()->MutableMessage(cfg, field_desc));
                }
            }
        }

        if(!base_cfg_)
        {
            base_cfg_ = new AppBaseConfig;
            own_base_cfg_ = true;
        }
        
            
        base_cfg_->set_app_name(application_name);
        // incorporate some parts of the AppBaseConfig that are common
        // with gobyd (e.g. Verbosity)
        merge_app_base_cfg(base_cfg_, var_map);

    }
    catch(common::ConfigException& e)
    {
        // output all the available command line options
        if(e.error())
        {
            std::cerr << od << "\n";
            std::cerr << "Problem parsing command-line configuration: \n"
                      << e.what() << "\n";
        }
        throw;
    }
    
    // set up the logger
   glog.set_name(application_name());
   glog.add_stream(static_cast<common::logger::Verbosity>(base_cfg_->glog_config().tty_verbosity()), &std::cout);

   if(base_cfg_->glog_config().show_gui())
       glog.enable_gui();

   fout_.resize(base_cfg_->glog_config().file_log_size());
   for(int i = 0, n = base_cfg_->glog_config().file_log_size(); i < n; ++i)
   {
       using namespace boost::posix_time;

       boost::format file_format(base_cfg_->glog_config().file_log(i).file_name());
       file_format.exceptions( boost::io::all_error_bits ^ ( boost::io::too_many_args_bit | boost::io::too_few_args_bit)); 

       std::string file_name = (file_format % to_iso_string(second_clock::universal_time())).str();
       std::string file_symlink = (file_format % "latest").str();

       glog.is(VERBOSE) &&
           glog << "logging output to file: " << file_name << std::endl;

       fout_[i].reset(new std::ofstream(file_name.c_str()));
       
       if(!fout_[i]->is_open())           
           glog.is(DIE) && glog << die << "cannot write glog output to requested file: " << file_name << std::endl;

       remove(file_symlink.c_str());
       namespace fs = boost::filesystem;
       fs::path canonicalized_file_name = fs::canonical(fs::path(file_name.c_str()));
       symlink(canonicalized_file_name.string().c_str(), file_symlink.c_str());       
        
       
       glog.add_stream(base_cfg_->glog_config().file_log(i).verbosity(), fout_[i].get());
   } 
   
   
    if(!base_cfg_->IsInitialized())
        throw(common::ConfigException("Invalid base configuration"));
    
   glog.is(DEBUG1) && glog << "App name is " << application_name() << std::endl;
    
}

goby::common::ApplicationBase::~ApplicationBase()
{
    glog.is(DEBUG1) && glog <<"ApplicationBase destructing..." << std::endl;

    if(own_base_cfg_)
        delete base_cfg_;
}

void goby::common::ApplicationBase::__run()
{
    // block SIGWINCH (change window size) in all threads
    sigset_t signal_mask;
    sigemptyset (&signal_mask);
    sigaddset (&signal_mask, SIGWINCH);
    pthread_sigmask (SIG_BLOCK, &signal_mask, NULL);

    // continue to run while we are alive (quit() has not been called)
    while(alive_)
    {
//        try
//        {            
            iterate();
//        }
//        catch(std::exception& e)
//        {
            // glog.is(WARN) &&
            //     glog << e.what() << std::endl;
//        }
    }

}

