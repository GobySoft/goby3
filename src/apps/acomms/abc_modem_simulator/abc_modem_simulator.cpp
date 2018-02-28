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

//
// Usage:
// 1. run abc_modem_simulator running on same port (as TCP server)
// > abc_modem_simulator 54321
// 2. create fake tty terminals connected to TCP as client to port 54321
// > socat -d -d -v pty,raw,echo=0,link=/tmp/ttyFAKE1 TCP:localhost:54321
// > socat -d -d -v pty,raw,echo=0,link=/tmp/ttyFAKE2 TCP:localhost:54321
// > ...
// 3. run your application connecting to /tmp/ttyFAKE1, /tmp/ttyFAKE2, etc. They will all act in the same "broadcast" pool

#include <map>
#include <string>

#include "goby/util/linebasedcomms.h"
#include "goby/util/as.h"
#include "goby/acomms/acomms_constants.h" // for BROADCAST_ID

std::map<int, std::string> modem_id2endpoint;

void parse_in(const std::string& in, std::map<std::string, std::string>* out);

int main(int argc, char* argv[])
{
    if(argc < 2)
    {
        std::cout << "usage: abc_modem_simulator [tcp listen port]" << std::endl;
        exit(1);    
    }

    goby::util::TCPServer server(goby::util::as<unsigned>(argv[1]));

    server.start();
    sleep(1);
    
    while(server.active())
    {
        goby::util::protobuf::Datagram in;
        while(server.readline(&in))
        {
            // clear off \r\n and other whitespace at ends
            boost::trim(*in.mutable_data());

            std::cout << "Received: " << in.ShortDebugString() << std::endl;
            
            std::map<std::string, std::string> parsed;
            try
            {
                parse_in(in.data(), &parsed);
                if(parsed["KEY"] == "CONF")
                {
                    std::cout << "Got configuration: " << in.data() << std::endl;

                    // register a modem id
                    if(parsed.count("MAC"))
                    {
                        int mac = goby::util::as<int>(parsed["MAC"]);
                        std::cout << "Set MAC address " << mac << " for endpoint " << in.src() << std::endl;
                        modem_id2endpoint[mac] = in.src();
                    }
                }
                else if(parsed["KEY"] == "SEND")
                {

                    std::cout << "Got send: " << in.data() << std::endl;
                    
                    goby::util::protobuf::Datagram out;        
                    out.set_src(server.local_endpoint());

                    if(!parsed.count("HEX"))
                        throw(std::runtime_error("No DATA in SEND message"));

                    if(!parsed.count("FROM"))
                        throw(std::runtime_error("No FROM in SEND message"));
                    
                    if(!parsed.count("BITRATE"))
                        throw(std::runtime_error("No BITRATE in SEND message"));
                    

                    int src = goby::util::as<int>(parsed["FROM"]);
                    
                    if(parsed.count("TO"))
                    {
                        int dest = goby::util::as<int>(parsed["TO"]);
                        
                        std::stringstream out_ss;
                        out_ss << "RECV,FROM:" << src
                               << ",TO:" << dest
                               << ",HEX:" << parsed["HEX"]
                               << ",BITRATE:" << parsed["BITRATE"] << "\r\n";
                        out.set_data(out_ss.str());
                        
                        if(dest == goby::acomms::BROADCAST_ID)
                        {                            
                            typedef std::map<int, std::string>::const_iterator const_iterator;
                            for(const_iterator it = modem_id2endpoint.begin(), n =  modem_id2endpoint.end(); it !=n; ++it)
                            {
                                // do not send it back to the originator
                                if(it->first != src)
                                {
                                    out.set_dest(it->second);
                                    std::cout << "Sending: " << out.ShortDebugString() << std::endl;
                                    server.write(out);
                                }
                            }
                        }
                        else
                        {
                            if(!modem_id2endpoint.count(dest))
                                throw(std::runtime_error("Unknown destination ID " + goby::util::as<std::string>(dest)));

                            out.set_dest(modem_id2endpoint[dest]);
                            std::cout << "Sending: " << out.ShortDebugString() << std::endl;
                            server.write(out);

                            if(parsed.count("ACK") && goby::util::as<bool>(parsed["ACK"]))
                            {
                                out.set_dest(in.src());
                                
                                std::stringstream out_ss;
                                out_ss << "ACKN,FROM:" << dest
                                       << ",TO:" << src << "\r\n";
                                out.set_data(out_ss.str());
                                std::cout << "Sending: " << out.ShortDebugString() << std::endl;

                                server.write(out);
                            }
                        }
                        
                    }
                    else 
                        throw(std::runtime_error("No TO in SEND message"));

                } 
            }
            catch(std::exception& e)
            {
                std::cout << "Invalid line from modem: " << in.data() << std::endl;
                std::cout << "Why: " << e.what() << std::endl;
            }
        }
        
        usleep(1000);
    }

    std::cout << "server failed..." << std::endl;
    exit(1);
}


void parse_in(const std::string& in, std::map<std::string, std::string>* out)
{
    std::vector<std::string> comma_split;
    boost::split(comma_split, in, boost::is_any_of(","));
    out->insert(std::make_pair("KEY", comma_split.at(0)));
    for(int i = 1, n = comma_split.size(); i < n; ++i)
    {
        std::vector<std::string> colon_split;
        boost::split(colon_split, comma_split[i],
                     boost::is_any_of(":"));
        out->insert(std::make_pair(colon_split.at(0),
                                   colon_split.at(1)));
    }
}

