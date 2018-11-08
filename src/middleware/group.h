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

#ifndef Group20170807H
#define Group20170807H

#include <string>
#include <memory>

namespace goby
{
    class Group
    {
    public:
        constexpr Group(const char* c = "") : c_(c) { }
        constexpr Group(int i) : i_(i) { }

        constexpr int numeric() const { return i_; }
        constexpr const char* c_str() const { return c_; }


        operator std::string() const
        {
            if(c_ != nullptr) return std::string(c_);
            else return std::to_string(i_);
        }


    protected:
	void set_c_str(const char* c) { c_ = c; }

    private:
        int i_{0};
        const char* c_{nullptr};
    };

    inline bool operator==(const Group& a, const Group& b)
    { return std::string(a) == std::string(b); }

    inline bool operator!=(const Group& a, const Group& b)
    { return !(a == b); }

    template<const Group& group>
        void check_validity()
    {
        static_assert((group.numeric() != 0) || (group.c_str()[0] != '\0'), "goby::Group must have non-zero length string or non-zero integer value.");
    }

    inline void check_validity_runtime(const Group& group)
    {
        // currently no-op - InterVehicleTransporterBase allows empty groups
        // TODO - check if there's anything we can check for here
    }

    inline std::ostream& operator<<(std::ostream& os, const Group& g)
    { return(os << std::string(g)); }

    class DynamicGroup : public Group
    {
    public:
    DynamicGroup(const std::string& s) : s_(new std::string(s))
        {
            Group::set_c_str(s_->c_str());
        }

    DynamicGroup(int i) : Group(i) { }

    private:
	std::unique_ptr<const std::string> s_;
    };

}


#endif
