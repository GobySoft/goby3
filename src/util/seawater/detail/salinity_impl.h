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

// modified for C++ by s. petillo spetillo@mit.edu
// ocean engineering graduate student - mit / whoi joint program
// massachusetts institute of technology (mit)
// laboratory for autonomous marine sensing systems (lamss)

//Algorithm from S. Petillo salinity.m
// Function for calculation of salinity from conductivity.
//
// S.Petillo (spetillo@mit.edu) - 02 Aug. 2010
//
// Taken directly from:
// 'Algorithms for computation of fundamental properties of seawater'
// UNESCO 1983 equation
// conductivity ratio to practical salinity conversion (SAL78)
//
// Valid over the ranges:
// Temperaturature: -2<T<35 degC
// Salinity: 2<S<42 psu
// (from Prekin and Lewis, 1980)
//
// Units:
// conductivity Cond mS/cm
// salinity S (PSS-78)
// temperature T (IPTS-68) degC
// pressure P decibars

// Cond(35,15,0) = 42.914 mS/cm
// the electrical conductivity of the standard seawater.
// Given by Jan Schultz (23 May, 2008) in
// 'Conversion between conductivity and PSS-78 salinity' at
// http://www.code10.info/index.php?option=com_content&view=category&id=54&Itemid=79

#ifndef SALINITY_IMPL_20190606H
#define SALINITY_IMPL_20190606H

namespace goby
{
namespace util
{
namespace seawater
{
namespace detail
{
class SalinityCalculator
{
  public:
    enum
    {
        TO_SALINITY = false,
        FROM_SALINITY = true
    };
    // returns salinity or conductivity, based on value of M
    static double compute(double CND, double T, double P, bool M)
    {
        double SAL78 = 0;

        // ZERO SALINITY/CONDUCTIVITY TRAP
        if (((M == 0) && (CND <= 5e-4)) || ((M == 1) && (CND <= 0.2)))
            return SAL78;

        double DT = T - 15;

        // SELECT BRANCH FOR SALINITY (M=0) OR CONDUCTIVITY (M=1)
        if (M == 0)
        {
            // CONVERT CONDUCTIVITY TO SALINITY
            double Res = CND;
            double RT = Res / (RT35(T) * (1.0 + C(P) / (B(T) + A(T) * Res)));
            RT = std::sqrt(std::abs(RT));
            SAL78 = SAL(RT, DT);
            return SAL78;
        }
        else
        {
            // INVERT SALINITY TO CONDUCTIVITY BY THE
            // NEWTON-RAPHSON ITERATIVE METHOD
            // FIRST APPROXIMATION

            double RT = std::sqrt(CND / 35);
            double SI = SAL(RT, DT);
            double N = 0;

            // TIERATION LOOP BEGINS HERE WITH A MAXIMUM OF 10 CYCLES
            double DELS = 0;
            do
            {
                RT = RT + (CND - SI) / DSAL(RT, DT);
                SI = SAL(RT, DT);
                N = N + 1;
                DELS = std::abs(SI - CND);

            } while ((DELS > 1e-4) && (N < 10));

            //COMPUTE CONDUCTIVITY RATIO
            double RTT = RT35(T) * RT * RT;
            double AT = A(T);
            double BT = B(T);
            double CP = C(P);
            CP = RTT * (CP + BT);
            BT = BT - RTT * AT;

            // SOLVE QUADRATIC EQUATION FOR R: R=RT35*RT*(1+C/AR+B)
            //    R  := SQRT (ABS (BT * BT + 4.0 * AT * CP)) - BT;
            double Res = std::sqrt(std::abs(BT * BT + 4 * AT * CP)) - BT;
            // CONDUCTIVITY RETURN
            SAL78 = 0.5 * Res / AT;
            return SAL78;
        }
    }

  private:
    static double SAL(double XR, double XT)
    {
        // PRACTICAL SALINITY SCALE 1978 DEFINITION WITH TEMPERATURE
        // CORRECTION;XT :=T-15.0; XR:=SQRT(RT);
        double sal =
            ((((2.7081 * XR - 7.0261) * XR + 14.0941) * XR + 25.3851) * XR - 0.1692) * XR + 0.0080 +
            (XT / (1.0 + 0.0162 * XT)) *
                (((((-0.0144 * XR + 0.0636) * XR - 0.0375) * XR - 0.0066) * XR - 0.0056) * XR +
                 0.0005);
        return sal;
    }

    static double DSAL(double XR, double XT)
    {
        // FUNCTION FOR DERIVATIVE OF SAL(XR,XT) WITH XR
        double dsal = ((((13.5405 * XR - 28.1044) * XR + 42.2823) * XR + 50.7702) * XR - 0.1692) +
                      (XT / (1.0 + 0.0162 * XT)) *
                          ((((-0.0720 * XR + 0.2544) * XR - 0.1125) * XR - 0.0132) * XR - 0.0056);
        return dsal;
    }

    static double RT35(double XT)
    {
        // FUNCTION RT35: C(35,T,0)/C(35,15,0) VARIATION WITH TEMPERATURE
        double rt35 =
            (((1.0031E-9 * XT - 6.9698E-7) * XT + 1.104259E-4) * XT + 2.00564E-2) * XT + 0.6766097;
        return rt35;
    }

    static double C(double XP)
    {
        // C(XP) POLYNOMIAL CORRESPONDS TO A1-A3 CONSTANTS: LEWIS 1980
        double c = ((3.989E-15 * XP - 6.370E-10) * XP + 2.070E-5) * XP;
        return c;
    }

    static double B(double XT)
    {
        double b = (4.464E-4 * XT + 3.426E-2) * XT + 1.0;
        return b;
    }

    static double A(double XT)
    {
        //A(XT) POLYNOMIAL CORRESPONDS TO B3 AND B4 CONSTANTS: LEWIS 1980
        double a = -3.107E-3 * XT + 0.4215;
        return a;
    }

    SalinityCalculator();
    ~SalinityCalculator();
    SalinityCalculator(const SalinityCalculator&);
    SalinityCalculator& operator=(const SalinityCalculator&);
};
} // namespace detail
} // namespace seawater
} // namespace util
} // namespace goby

#endif
