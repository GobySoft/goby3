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

#ifndef MESSAGE_VAR_FLOAT20100317H
#define MESSAGE_VAR_FLOAT20100317H

#include "goby/util/as.h"

#include "message_var.h"

#include <boost/math/special_functions/fpclassify.hpp>

namespace goby
{
    namespace transitional
    {   
        class DCCLMessageVarFloat : public DCCLMessageVar
        {
          public:
            DCCLMessageVarFloat(double max = std::numeric_limits<double>::max(), double min = 0, double precision = 0);

        
            void set_max(double max) {max_ = max;}
            void set_max(const std::string& s) { set_max(util::as<double>(s)); }
        
            void set_min(double min) {min_ = min;}
            void set_min(const std::string& s) { set_min(util::as<double>(s)); }

            void set_precision(int precision) {precision_ = precision;}
            void set_precision(const std::string& s) { set_precision(util::as<int>(s)); }
        
            int precision() const { return precision_; }  

            double min() const { return min_; }
            double max() const { return max_; }
        
            void set_max_delta(double max_delta)
            { max_delta_ = max_delta; }
            void set_max_delta(const std::string& s)
            { set_max_delta(util::as<double>(s)); }

            virtual DCCLType type() const { return dccl_float; }


          protected:
            virtual DCCLMessageVal cast(double d, int precision) { return DCCLMessageVal(d, precision); }
            virtual void initialize_specific();
            
            virtual void pre_encode(DCCLMessageVal& v);
            
            
          private:
            bool is_delta() const
            { return using_delta_differencing() && !is_key_frame_; }
        
            bool using_delta_differencing() const
            { return !(boost::math::isnan)(max_delta_); }

        
          private:
            double max_;
            double min_;
            int precision_;

            double max_delta_;
        };

    }
}
#endif
