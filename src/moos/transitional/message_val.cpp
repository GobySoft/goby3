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

#include <stdexcept>
#include <iomanip>

#include <boost/foreach.hpp>

#include <boost/lexical_cast.hpp>
#include <boost/numeric/conversion/cast.hpp>

#include "goby/util/as.h"
#include "goby/util/sci.h"

#include "message_val.h"
#include "goby/acomms/dccl.h"

using goby::util::as;


void goby::transitional::DCCLMessageVal::init()
{
    sval_ = "";
    dval_ = 0;
    lval_ = 0;
    bval_ = false;
    precision_ = MAX_DBL_PRECISION;
    type_ = cpp_notype;
}


goby::transitional::DCCLMessageVal::DCCLMessageVal()
{ init(); }


goby::transitional::DCCLMessageVal::DCCLMessageVal(const std::string& s)
{
    init();
    sval_ = s;
    type_ = cpp_string;
}


goby::transitional::DCCLMessageVal::DCCLMessageVal(const char* s)
{
    init();
    sval_ = s;
    type_ = cpp_string;
}


goby::transitional::DCCLMessageVal::DCCLMessageVal(double d, int p /* = MAX_DBL_PRECISION*/ )
{
    init();
    dval_ = d;
    precision_ = p;
    type_ = cpp_double;
}

goby::transitional::DCCLMessageVal::DCCLMessageVal(float f)
{
    init();
    dval_ = f;
    type_ = cpp_double;
}

goby::transitional::DCCLMessageVal::DCCLMessageVal(long l)
{
    init();
    lval_ = l;
    type_ = cpp_long;
}        

goby::transitional::DCCLMessageVal::DCCLMessageVal(int i)
{
    init();
    lval_ = i;
    type_ = cpp_long;
}        

goby::transitional::DCCLMessageVal::DCCLMessageVal(bool b)
{
    init();
    bval_ = b;
    type_ = cpp_bool;
}        


goby::transitional::DCCLMessageVal::DCCLMessageVal(const std::vector<DCCLMessageVal>& vm)
{
    if(vm.size() != 1)
        throw(goby::acomms::DCCLException("vector cast to DCCLMessageVal failed: vector is not size 1"));
    else
        *this = vm[0];
}


void goby::transitional::DCCLMessageVal::set(std::string sval)
{ sval_ = sval; type_ = cpp_string; }
void goby::transitional::DCCLMessageVal::set(double dval, int precision /* = MAX_DBL_PRECISION */)
{ dval_ = dval; type_ = cpp_double; precision_ = precision; }
void goby::transitional::DCCLMessageVal::set(long lval)
{ lval_ = lval; type_ = cpp_long; }
void goby::transitional::DCCLMessageVal::set(bool bval)
{ bval_ = bval; type_ = cpp_bool; }


bool goby::transitional::DCCLMessageVal::get(std::string& s) const
{
    std::stringstream ss;
    switch(type_)
    {
        case cpp_string:
            s = sval_;
            return true;            

        case cpp_double:
            if((log10(abs(dval_)) + precision_) <= MAX_DBL_PRECISION) 
                ss << std::fixed << std::setprecision(precision_) << dval_;
            else
                ss << std::setprecision(precision_) << dval_;
            
            s = ss.str();
            return true;            

        case cpp_long:
            s = as<std::string>(lval_);
            return true;

        case cpp_bool:
            s = (bval_) ? "true" : "false";
            return true;

        default:
            return false;
    }
}

bool goby::transitional::DCCLMessageVal::get(bool& b) const
{
    switch(type_)
    {
        case cpp_string:
            if(boost::iequals(sval_, "true") || boost::iequals(sval_, "1"))
                b = true;
            else if(boost::iequals(sval_, "false") || boost::iequals(sval_, "0"))
                b = false;
            else
                return false;
            return true;
            
        case cpp_double:
            try { b = boost::numeric_cast<bool>(dval_); }
            catch(...) { return false; }
            return true;

        case cpp_long:
            try { b = boost::numeric_cast<bool>(lval_); }
            catch(...) { return false; }
            return true;

        case cpp_bool:
            b = bval_;
            return true;

        default:
            return false;
    }
}    

bool goby::transitional::DCCLMessageVal::get(long& t) const
{
    switch(type_)
    {
        case cpp_string:
            try
            {
                double d = boost::lexical_cast<double>(sval_);
                t = boost::numeric_cast<long>(util::unbiased_round(d, 0));
            }
            catch (...)
            {
                if(boost::iequals(sval_, "true"))
                    t = 1;
                else if(boost::iequals(sval_, "false"))
                    t = 0;
                else
                    return false;
            }
            return true;

        case cpp_double:
            try { t = boost::numeric_cast<long>(util::unbiased_round(dval_, 0)); }
            catch(...) { return false; }
            return true;

        case cpp_long:
            t = lval_;
            return true;

        case cpp_bool:
            t = (bval_) ? 1 : 0;
            return true;

        default:
            return false;
    }
}        
        
bool goby::transitional::DCCLMessageVal::get(double& d) const
{
    switch(type_)
    {
        case cpp_string:
            try { d = boost::lexical_cast<double>(sval_); }
            catch (boost::bad_lexical_cast &)
            {
                if(boost::iequals(sval_, "true"))
                    d = 1;
                else if(boost::iequals(sval_, "false"))
                    d = 0;
                else
                    return false;
            }
            return true;

        case cpp_double:
            d = dval_;
            return true;

        case cpp_long:
            try { d = boost::numeric_cast<double>(lval_); }
            catch (...) { return false; }
            return true;

        case cpp_bool:
            d = (bval_) ? 1 : 0;
            return true;

        default:
            return false;

    }
}




goby::transitional::DCCLMessageVal::operator double() const
{
    double d;
    if(get(d)) return d;
    else return std::numeric_limits<double>::quiet_NaN();
}


goby::transitional::DCCLMessageVal::operator bool() const
{
    bool b;
    if(get(b)) return b;
    else return false;
}

goby::transitional::DCCLMessageVal::operator std::string() const
{
    std::string s;
    if(get(s)) return s;
    else return "";
}

goby::transitional::DCCLMessageVal::operator long() const
{
    long l;
    if(get(l)) return l;
    else return 0;
}

goby::transitional::DCCLMessageVal::operator int() const
{
    return long(*this);
}

goby::transitional::DCCLMessageVal::operator unsigned() const
{
    return long(*this);
}

goby::transitional::DCCLMessageVal::operator float() const
{
    return double(*this);
}

goby::transitional::DCCLMessageVal::operator std::vector<goby::transitional::DCCLMessageVal>() const
{
    return std::vector<DCCLMessageVal>(1, *this);
}

bool goby::transitional::DCCLMessageVal::operator==(const DCCLMessageVal& mv) const
{
    switch(mv.type_)
    {
        case cpp_string: return mv == sval_;
        case cpp_double: return mv == dval_;
        case cpp_long:   return mv == lval_;
        case cpp_bool:   return mv == bval_;
        default:         return false;
    }
}

bool goby::transitional::DCCLMessageVal::operator==(const std::string& s)  const
{
    std::string us;
    return get(us) && us == s;
}

bool goby::transitional::DCCLMessageVal::operator==(double d) const
{
    double us;
    return get(us) && us == d;
}

bool goby::transitional::DCCLMessageVal::operator==(long l) const
{
    long us;
    return get(us) && us == l;
}

bool goby::transitional::DCCLMessageVal::operator==(bool b) const
{
    bool us;
    return get(us) && us == b;
}        



std::ostream& goby::transitional::operator<<(std::ostream& os, const goby::transitional::DCCLMessageVal& mv)
{
    switch(mv.type_)
    {
        case cpp_string: return os << "std::string: " << mv.sval_;
        case cpp_double: return os << "double: " << std::fixed << std::setprecision(mv.precision_) << mv.dval_;
        case cpp_long:   return os << "long: " << mv.lval_;                
        case cpp_bool:   return os << "bool: " << std::boolalpha << mv.bval_;
        default:         return os << "{empty}";
    }
}

    
std::ostream& goby::transitional::operator<<(std::ostream& os, const std::vector<goby::transitional::DCCLMessageVal>& vm)
{
    int j=0;
    BOOST_FOREACH(const DCCLMessageVal& m, vm)
    {
        if(j) os << ",";
        os << m;
        ++j;        
    }
    return os;
}
