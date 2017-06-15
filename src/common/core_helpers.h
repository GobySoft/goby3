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

// copyright 2010-2011 t. schneider tes@mit.edu
// 
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This software is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this software.  If not, see <http://www.gnu.org/licenses/>

#ifndef CoreHelpers20110411H
#define CoreHelpers20110411H

#include "goby/common/protobuf/app_base_config.pb.h"
#include <boost/program_options.hpp>


namespace goby
{
    namespace common
    {
        /// provides stream output operator for Google Protocol Buffers Message 
        inline std::ostream& operator<<(std::ostream& out, const google::protobuf::Message& msg)
        {
            return (out << "### " << msg.GetDescriptor()->full_name() << " ###\n" << msg.DebugString());
        }


        template<class AppBaseConfig>
        void merge_app_base_cfg(AppBaseConfig* base_cfg,
                                       const boost::program_options::variables_map& var_map)
        {
            if(var_map.count("ncurses"))
            {
                base_cfg->mutable_glog_config()->set_show_gui(true);
            }
            else if (var_map.count("verbose"))
            {
                switch(var_map["verbose"].as<std::string>().size())
                {
                    default:
                    case 0:
                        base_cfg->mutable_glog_config()->set_tty_verbosity(common::protobuf::GLogConfig::VERBOSE);
                        break;
                    case 1:
                        base_cfg->mutable_glog_config()->set_tty_verbosity(common::protobuf::GLogConfig::DEBUG1);
                        break;
                    case 2:
                        base_cfg->mutable_glog_config()->set_tty_verbosity(common::protobuf::GLogConfig::DEBUG2);
                        break;
                    case 3:
                        base_cfg->mutable_glog_config()->set_tty_verbosity(common::protobuf::GLogConfig::DEBUG3);
                        break;
                }
            }

//            if(var_map.count("no_db"))
//                base_cfg->mutable_database_config()->set_using_database(false);
        }

    }
}



#endif
