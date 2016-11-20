// Copyright 2009-2016 Toby Schneider (http://gobysoft.org/index.wt/people/toby)
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

#ifndef APPLICATIONBASE320161120H
#define APPLICATIONBASE320161120H

#include <iostream>

#include <boost/shared_ptr.hpp>

#include "goby/common/exception.h"
#include "goby/common/configuration_reader.h"

#include "goby/common/protobuf/app_base_config.pb.h"

#include "goby/common/logger.h"

namespace goby
{
    /// \brief Run a Goby application derived from MinimalApplicationBase.
    /// blocks caller until MinimalApplicationBase::__run() returns
    /// \param argc same as int main(int argc, char* argv)
    /// \param argv same as int main(int argc, char* argv)
    /// \return same as int main(int argc, char* argv)

    template<typename App, typename Config>
        int run(int argc, char* argv[], Config* cfg);
    
    namespace common
    {
        class ApplicationBase3
        {
          public:
            ApplicationBase3(google::protobuf::Message* cfg = 0);
            virtual ~ApplicationBase3();

            /// \brief Requests a clean (return 0) exit.
            void quit() { alive_ = false; }

            virtual void run() = 0;
            
            /// name of this application (from AppBaseConfig::app_name). E.g. "garmin_gps_g"
            std::string application_name()
            { return base_cfg_->app_name(); }
            /// name of this platform (from AppBaseConfig::platform_name). E.g. "AUV-23" or "unicorn"
            std::string platform_name()
            { return base_cfg_->platform_name(); }

            
            template<typename App, typename Config>
                friend int ::goby::run(int argc, char* argv[], Config* cfg);

            const AppBaseConfig& base_cfg()
            { return *base_cfg_; }


          private:
            
            // main loop that exits on disconnect. called by goby::run()
            void __run();
            
            void __set_application_name(const std::string& s)
            { base_cfg_->set_app_name(s); }
            void __set_platform_name(const std::string& s)
            { base_cfg_->set_platform_name(s); }
            
            
          private:
                
            // copies of the "real" argc, argv that are used
            // to give ApplicationBase3 access without requiring the subclasses of
            // ApplicationBase3 to pass them through their constructors
            static int argc_;
            static char** argv_;
            google::protobuf::Message* all_cfg_;

            // configuration relevant to all applications (loop frequency, for example)
            // defined in #include "proto/app_base_config.pb.h"
            AppBaseConfig* base_cfg_;
            bool own_base_cfg_;
            
            bool alive_;            
            std::vector<boost::shared_ptr<std::ofstream> > fout_;
        };
    }
}


template<typename App, typename Config>
    int goby::run(int argc, char* argv[], Config* cfg)
{    
    // avoid making the user pass these through their Ctor...
    App::argc_ = argc;
    App::argv_ = argv;

    try
    {
        App app(cfg);
        app.__run();
    }
    catch(goby::common::ConfigException& e)
    {
        // no further warning as the ApplicationBase3 Ctor handles this
        return 1;
    }
    catch(std::exception& e)
    {
        // some other exception
        std::cerr << "uncaught exception: " << e.what() << std::endl;
        return 2;
    }

    return 0;
}

#endif
