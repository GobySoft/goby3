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

#include "message_var_float.h"

#include "goby/util/sci.h"

goby::transitional::DCCLMessageVarFloat::DCCLMessageVarFloat(double max /*= std::numeric_limits<double>::max()*/, double min /*= 0*/, double precision /*= 0*/)
    : DCCLMessageVar(),
      max_(max),
      min_(min),
      precision_(precision),
      max_delta_(std::numeric_limits<double>::quiet_NaN())
{ }

        
void goby::transitional::DCCLMessageVarFloat::initialize_specific()
{
    // flip max and min if needed
    if(max_ < min_)
    {
        double tmp = max_;
        max_ = min_;
        min_ = tmp;
    } 

    if(using_delta_differencing() && max_delta_ < 0)
        max_delta_ = -max_delta_;
}

void goby::transitional::DCCLMessageVarFloat::pre_encode(DCCLMessageVal& v)
{
    double r;
    if(v.get(r)) v = util::unbiased_round(r,precision_);
}

