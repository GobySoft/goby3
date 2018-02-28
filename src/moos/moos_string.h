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

#ifndef MOOSSTRING20110527H
#define MOOSSTRING20110527H

#include "goby/moos/moos_header.h"


#include "goby/util/as.h"
#include "goby/common/time.h"

namespace goby
{
    namespace moos
    {
        
        inline std::ostream& operator<<(std::ostream& os, const CMOOSMsg& msg)
        {
            os << "[[CMOOSMsg]]" << " Key: " << msg.GetKey()
               << " Type: "
               << (msg.IsDouble() ? "double" : "string")
               << " Value: " << (msg.IsDouble() ? goby::util::as<std::string>(msg.GetDouble()) : msg.GetString())
               << " Time: " << goby::util::as<boost::posix_time::ptime>(msg.GetTime())
               << " Community: " << msg.GetCommunity() 
               << " Source: " << msg.m_sSrc // no getter in CMOOSMsg!!!
               << " Source Aux: " << msg.GetSourceAux();
            return os;
        }

        /// case insensitive - find `key` in `str` and if successful put it in out
        /// and return true
        /// deal with these basic forms:
        /// str = foo=1,bar=2,pig=3
        /// str = foo=1,bar={2,3,4,5},pig=3
        /// \param out string to return value in
        /// \param str haystack to search
        /// \param key needle to find
        /// \return true if key is in str, false otherwise
        inline bool val_from_string(std::string& out, const std::string& str, const std::string & key)
        {
            // str:  foo=1,bar={2,3,4,5},pig=3,cow=yes
            // two cases:
            // key:  bar
            // key:  pig
            
            out.erase();

            
            // deal with foo=bar,o=bar problem when looking for "o=" since
            // o is contained in foo

            // ok: beginning of string, end of string, comma right before start_pos
            int match = 0;
            std::string::size_type start_pos = 0;
            while(match == 0 || !(start_pos == 0 || start_pos == std::string::npos || str[start_pos-1] == ','))
            {
                // str:  foo=1,bar={2,3,4,5},pig=3,cow=yes
                //  start_pos  ^             ^
                boost::iterator_range<std::string::const_iterator> result = boost::ifind_nth(str, std::string(key+"="), match);
                start_pos = (result) ? result.begin() - str.begin() : std::string::npos;
                ++match;
            }
            
            if(start_pos != std::string::npos)
            {
                // chopped:   bar={2,3,4,5},pig=3,cow=yes
                // chopped:   pig=3,cow=yes
                std::string chopped = str.substr(start_pos);

                // chopped:  bar={2,3,4,5},pig=3,cow=yes
                // equal_pos    ^         
                // chopped:  pig=3,cow=yes
                // equal_pos    ^         
                std::string::size_type equal_pos = chopped.find("=");

                // check for array
                bool is_array = (equal_pos+1 < chopped.length() && chopped[equal_pos+1] == '{');
            
                if(equal_pos != std::string::npos)
                {
                    // chopped_twice:  ={2,3,4,5},pig=3,cow=yes
                    // chopped_twice:  =pig=3,cow=yes              
                    std::string chopped_twice = chopped.substr(equal_pos);

                    // chopped_twice:  ={2,3,4,5},pig=3,cow=yes  
                    // end_pos                  ^     
                    // chopped_twice:  =pig=3,cow=yes
                    // end_pos               ^         
                    std::string::size_type end_pos =
                        (is_array) ? chopped_twice.find("}") : chopped_twice.find(",");

                    // out:  2,3,4,5
                    // out:  3
                    out = (is_array) ? chopped_twice.substr(2, end_pos-2) : chopped_twice.substr(1, end_pos-1);

                    return true;
                }
            }

            return false;        
        }        

        /// variation of val_from_string() for arbitrary return type
        /// \return false if `key` not in `str` OR if `out` is not of proper type T
        template<typename T>
            inline bool val_from_string(T& out, const std::string& str, const std::string & key)        
        {
            std::string s;
            if(!val_from_string(s, str, key)) return false;
            
            try { out = boost::lexical_cast<T>(s); }
            catch (boost::bad_lexical_cast&)
            { return false; }

            return true;            
        }
        

        /// specialization of val_from_string for boolean `out`
        inline bool val_from_string(bool& out, const std::string& str, const std::string & key)
        {
            std::string s;
            if(!val_from_string(s, str, key)) return false;

            out = goby::util::as<bool>(s);
            return true;            
        }        
        //@}
        
        /// remove all blanks from string s    
        inline void stripblanks(std::string& s)
        {
            for(std::string::iterator it=s.end()-1; it!=s.begin()-1; --it)
            {    
                if(isspace(*it))
                    s.erase(it);
            }
        }
    }
}

#endif
