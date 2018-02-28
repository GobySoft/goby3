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

#ifndef AbcFrontSeat20130220H
#define AbcFrontSeat20130220H

#include <boost/bimap.hpp>

#include "goby/util/linebasedcomms/tcp_client.h"

#include "goby/moos/frontseat/frontseat.h"

#include "abc_frontseat_driver_config.pb.h"


extern "C"
{
    FrontSeatInterfaceBase* frontseat_driver_load(iFrontSeatConfig*);
}

class AbcFrontSeat : public FrontSeatInterfaceBase
{
  public:
    AbcFrontSeat(const iFrontSeatConfig& cfg);
    
  private: // virtual methods from FrontSeatInterfaceBase
    void loop();

    void send_command_to_frontseat(const goby::moos::protobuf::CommandRequest& command);
    void send_data_to_frontseat(const goby::moos::protobuf::FrontSeatInterfaceData& data);
    void send_raw_to_frontseat(const goby::moos::protobuf::FrontSeatRaw& data);
    goby::moos::protobuf::FrontSeatState frontseat_state() const;
    bool frontseat_providing_data() const;
    
  private: // internal non-virtual methods
    void check_connection_state();

    void try_receive();
    void process_receive(const std::string& s);
    void parse_in(const std::string& in, std::map<std::string, std::string>* out);

    void write(const std::string& s);
    
  private:
    const ABCFrontSeatConfig abc_config_;
    goby::util::TCPClient tcp_;

    bool frontseat_providing_data_;
    double last_frontseat_data_time_;
    goby::moos::protobuf::FrontSeatState frontseat_state_;

    goby::moos::protobuf::CommandRequest last_request_;    
};

#endif
