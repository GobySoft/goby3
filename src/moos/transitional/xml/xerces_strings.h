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

// this xml code based largely on work in C++ Cookbook by D. Ryan Stephens, Christopher Diggins, Jonathan Turkanis, and Jeff Cogswell. Copyright 2006 O'Reilly Media, INc., 0-596-00761-2.

// these functions define an easy way to convert from and to the XercesString
// unicode format to the native (ASCII in our case) string format.

#ifndef XERCES_STRINGS20091211H
#define XERCES_STRINGS20091211H

#include <string>

#include <boost/scoped_ptr.hpp>
#include <xercesc/util/XMLString.hpp>

// typedef std::basic_string<XMLCh> XercesString;

/* // Converts from a narrow-character string to a wide-character string. */
 inline const XMLCh* fromNative(const char* str) 
 { 
   return xercesc::XMLString::transcode(str);
 }

/* // Converts from a narrow-character string to a wide-charactr string. */
/* inline XercesString fromNative(const std::string& str) */
/* { */
/*     return fromNative(str.c_str()); */
/* } */

// Converts from a wide-character string to a narrow-character string.
inline std::string toNative(const XMLCh* str)
{
    boost::scoped_ptr<char> ptr(xercesc::XMLString::transcode(str));
    return std::string(ptr.get());
}

/* // Converts from a wide-character string to a narrow-character string. */
/* inline std::string toNative(const XercesString& str) */
/* { */
/*     return toNative(str.c_str()); */
/* } */

#endif // #ifndef XERCES_STRINGS_HPP_INCLUDED
