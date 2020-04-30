// Copyright 2009-2018 Toby Schneider (http://gobysoft.org/index.wt/people/toby)
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

#include "modem_id_convert.h"
#include "goby/util/as.h"
#include "goby/util/string.h"
#include <boost/algorithm/string.hpp>
#include <string>
#include <vector>

using namespace std;
using goby::util::as;

std::string goby::moos::ModemIdConvert::read_lookup_file(string path)
{
    stringstream ss;
    ss << "***********************************" << endl;
    ss << "modem_id_convert trying to read modem id lookup file" << endl;

    ifstream fin;

    fin.open(path.c_str(), ifstream::in);

    if (fin.fail())
    {
        string message = "cannot open " + path + " for reading!";
        ss << message << endl;
        return ss.str();
    }

    ss << "reading in modem id lookup table:" << endl;

    while (fin)
    {
        string sline;
        getline(fin, sline);

        // strip the spaces and comments out
        string::size_type pos = sline.find(' ');
        while (pos != string::npos)
        {
            sline.erase(pos, 1);
            pos = sline.find(' ');
        }
        pos = sline.find('#');
        while (pos != string::npos)
        {
            sline.erase(pos);
            pos = sline.find('#');
        }
        pos = sline.find("//");
        while (pos != string::npos)
        {
            sline.erase(pos);
            pos = sline.find("//");
        }

        // ignore blank lines
        if (sline != "")
        {
            vector<string> line_parsed;
            goby::util::split(line_parsed, sline, boost::is_any_of(","));

            if (line_parsed.size() < 3)
                ss << "invalid line: " << sline << endl;
            else
            {
                string location_name;
                if (line_parsed.size() < 4)
                    location_name = line_parsed[1];
                else
                    location_name = line_parsed[3];

                ss << "modem id [" << line_parsed[0] << "], name [" << line_parsed[1] << "], type ["
                   << line_parsed[2] << "]"
                   << ", location name [" << location_name << "]" << endl;

                int id = atoi(line_parsed[0].c_str());

                //add the entry to our lookup vectors

                names_[id] = line_parsed[1];

                max_name_length_ = max(names_[id].length(), max_name_length_);
                max_id_ = max(id, max_id_);

                types_[id] = line_parsed[2];
                locations_[id] = location_name;
            }
        }
    }

    fin.close();

    ss << "***********************************" << endl;
    return ss.str();
}

string goby::moos::ModemIdConvert::get_name_from_id(int id)
{
    if (names_.count(id))
        return names_[id]; // return the found name
    else
        return as<string>(id); // if not in the lookup table, just give the number back as a string
}

string goby::moos::ModemIdConvert::get_type_from_id(int id)
{
    if (types_.count(id))
        return types_[id];
    else
        return "unknown_type";
}

string goby::moos::ModemIdConvert::get_location_from_id(int id)
{
    if (locations_.count(id))
        return locations_[id];
    else
        return as<string>(id);
}

int goby::moos::ModemIdConvert::get_id_from_name(string name)
{
    for (map<int, string>::iterator it = names_.begin(); it != names_.end(); ++it)
    {
        if (boost::iequals(it->second, name))
            return it->first;
    }

    return int(as<double>(name));
}
