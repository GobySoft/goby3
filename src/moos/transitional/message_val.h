// Copyright 2009-2020:
//   GobySoft, LLC (2013-)
//   Massachusetts Institute of Technology (2007-2014)
//   Community contributors (see AUTHORS file)
// File authors:
//   Toby Schneider <toby@gobysoft.org>
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

#ifndef GOBY_MOOS_TRANSITIONAL_MESSAGE_VAL_H
#define GOBY_MOOS_TRANSITIONAL_MESSAGE_VAL_H

#include <iostream> // for ostream
#include <string>   // for string, basic_string
#include <vector>   // for vector

#include "dccl_constants.h" // for DCCLCppType, cpp_notype

namespace goby
{
namespace moos
{
namespace transitional
{
/// defines a DCCL value
class DCCLMessageVal
{
  public:
    enum
    {
        MAX_DBL_PRECISION = 15
    };

    /// \name Constructors/Destructor
    //@{
    /// empty
    DCCLMessageVal();

    /// construct with string value
    DCCLMessageVal(const std::string& s);

    /// construct with char* value
    DCCLMessageVal(const char* s);

    /// construct with double value, optionally givig the precision of the double (number of decimal places) which is used if a cast to std::string is required in the future.
    DCCLMessageVal(double d, int p = MAX_DBL_PRECISION);

    /// construct with long value
    DCCLMessageVal(long l);

    /// construct with int value
    DCCLMessageVal(int i);

    /// construct with float value
    DCCLMessageVal(float f);

    /// construct with bool value
    DCCLMessageVal(bool b);

    /// construct with vector
    DCCLMessageVal(const std::vector<DCCLMessageVal>& vm);

    //@}

    /// \name Setters
    //@{
    /// set the value with a string (overwrites previous value regardless of type)
    void set(std::string sval);
    /// \brief set the value with a double (overwrites previous value regardless of type)
    /// \param dval values to set
    /// \param precision decimal places of precision to preserve if this is cast to a string
    void set(double dval, int precision = MAX_DBL_PRECISION);
    /// set the value with a long (overwrites previous value regardless of type)
    void set(long lval);
    /// set the value with a bool (overwrites previous value regardless of type)
    void set(bool bval);

    //@}

    /// \name Getters
    //@{
    /// \brief extract as std::string (all reasonable casts are done)
    /// \param s std::string to store value in
    /// \return successfully extracted (and if necessary successfully cast to this type)
    bool get(std::string& s) const;
    /// \brief extract as bool (all reasonable casts are done)
    /// \param b bool to store value in
    /// \return successfully extracted (and if necessary successfully cast to this type)
    bool get(bool& b) const;
    /// \brief extract as long (all reasonable casts are done)
    /// \param t long to store value in
    /// \return successfully extracted (and if necessary successfully cast to this type)
    bool get(long& t) const;
    /// \brief extract as double (all reasonable casts are done)
    /// \param d double to store value in
    /// \return successfully extracted (and if necessary successfully cast to this type)
    bool get(double& d) const;

    ///
    /// allows statements of the form \code double d = DCCLMessageVal("3.23"); \endcode
    operator double() const;

    ///
    /// allows statements of the form \code bool b = DCCLMessageVal("1"); \endcode
    operator bool() const;

    ///
    /// allows statements of the form \code std::string s = DCCLMessageVal(3); \endcode
    operator std::string() const;

    ///
    /// allows statements of the form \code long l = DCCLMessageVal(5); \endcode
    operator long() const;

    ///
    /// allows statements of the form \code int i = DCCLMessageVal(2); \endcode
    operator int() const;

    ///
    /// allows statements of the form \code unsigned u = DCCLMessageVal(2); \endcode
    operator unsigned() const;

    ///
    /// allows statements of the form \code float f = DCCLMessageVal("3.5"); \endcode
    operator float() const;

    operator std::vector<DCCLMessageVal>() const;

    /// what type is the original type of this DCCLMessageVal?
    DCCLCppType type() const { return type_; }

    /// was this just constructed with DCCLMessageVal() ?
    bool empty() const { return type_ == cpp_notype; }

    unsigned precision() const { return precision_; }

    //@}

    /// \name Comparison
    //@{
    bool operator==(const DCCLMessageVal& mv) const;
    bool operator==(const std::string& s) const;
    bool operator==(double d) const;
    bool operator==(long l) const;
    bool operator==(bool b) const;

    // @}

  private:
    void init();

    friend std::ostream& operator<<(std::ostream& os, const DCCLMessageVal& mv);

  private:
    std::string sval_;
    double dval_;
    long lval_;
    bool bval_;

    unsigned precision_;

    DCCLCppType type_;
};

std::ostream& operator<<(std::ostream& os, const DCCLMessageVal& mv);
std::ostream& operator<<(std::ostream& os, const std::vector<DCCLMessageVal>& vm);
} // namespace transitional
} // namespace moos
} // namespace goby

#endif
