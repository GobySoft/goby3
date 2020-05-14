// Copyright 2011-2020:
//   GobySoft, LLC (2013-)
//   Massachusetts Institute of Technology (2007-2014)
//   Community contributors (see AUTHORS file)
// File authors:
//   Toby Schneider <toby@gobysoft.org>
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

#include "goby/util/as.h"

namespace goby
{
namespace test
{
namespace util
{
enum MyEnum
{
    BAZ = 0,
    FOO = 1,
    BAR = 2
};

class MyClass
{
  public:
    MyClass(int a = 0, std::string b = "") : a(a), b(b) {}

    bool operator==(const MyClass& other) { return (other.a == a) && (other.b == b); }

    friend std::istream& operator>>(std::istream& is, MyClass& obj);
    friend std::ostream& operator<<(std::ostream& os, const MyClass& obj);

  private:
    int a;
    std::string b;
};

std::istream& operator>>(std::istream& is, MyClass& obj)
{
    is >> obj.a;
    is.ignore(1);
    is >> obj.b;
    return is;
}
std::ostream& operator<<(std::ostream& os, const MyClass& obj)
{
    os << obj.a << "!" << obj.b;
    return os;
}

} // namespace util
} // namespace test
} // namespace goby

using namespace goby::test::util;
using goby::util::as;

template <typename A, typename B> void is_sane(A orig)
{
    std::cout << "Checking type A: " << typeid(A).name() << " converting to B: " << typeid(B).name()
              << std::endl;
    B converted = as<B>(orig);
    std::cout << "Original: " << orig << ", converted: " << converted << std::endl;
    A should_be_orig = as<A>(converted);
    std::cout << "Converted back from B to A: " << should_be_orig << std::endl;
    assert(should_be_orig == orig);
    std::cout << "ok!" << std::endl;
}

int main()
{
    // arithmetics
    assert(as<int>("12") == 12);
    assert(as<int>("12.7") == std::numeric_limits<int>::max());
    assert(as<int>("foo") == std::numeric_limits<int>::max());
    assert(as<double>("12.7") == double(12.7));
    assert(as<float>("12.7") == float(12.7));
    assert(as<double>("1e3") == 1e3);
    assert(std::isnan(as<double>("nan")));
    assert(std::isnan(as<double>("PIG")));

    // enums
    assert(as<MyEnum>("1") == FOO);
    assert(as<MyEnum>(2) == BAR);
    assert(as<MyEnum>("COW") == BAZ);

    // bool <--> string
    assert(as<bool>("TRUE") == true);
    assert(as<bool>("true") == true);
    assert(as<bool>("trUe") == true);
    assert(as<bool>("1") == true);
    assert(as<bool>("false") == false);
    assert(as<bool>("0") == false);
    assert(as<bool>("DOG") == false);
    assert(as<bool>("23") == false);

    assert(as<std::string>(true) == std::string("true"));
    assert(as<std::string>(false) == std::string("false"));

    // class
    assert(as<MyClass>("foobar") == MyClass());

    // two-way sanity checks

    is_sane<int, std::string>(3);
    is_sane<double, std::string>(3.56302);
    is_sane<float, std::string>(6.34);
    is_sane<unsigned, std::string>(12);
    is_sane<bool, std::string>(true);
    is_sane<MyEnum, std::string>(BAR);
    is_sane<MyClass, std::string>(MyClass(3, "cat"));

    std::cout << "all tests passed" << std::endl;

    return 0;
}
