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

#ifndef SWSTATEH
#define SWSTATEH

#include <cmath>

// Calculate water density anomaly at a given Salinity, Temperature, Pressure using the seawater Equation of State.
// Taken directly from MATLAB OceansToolbox swstate.m

inline double density_anomaly(double S, double T, double P0)
{
    /*

      SIGMA = density_anomaly(S,T,P) returns the density anomaly SIGMA (kg/m^3)
      given the salinity S (ppt), temperature T (deg C) and pressure
      P (dbars).
    
      Notes: RP (WHOI 2/dec/91)

      This stuff is directly copied from the UNESCO algorithms, with some
      minor changes to make it Matlab compatible (like adding ";" and changing
      "*" to "*" when necessary.

      RP  (WHOI 3/dec/91)

      ******************************************************
      SPECIFIC VOLUME ANOMALY (STERIC ANOMALY) BASED ON 1980 EQUATION
      OF STATE FOR SEAWATER AND 1978 PRACTICAL SALINITY SCALE.
      REFERENCES
      MILLERO, ET AL (1980) DEEP-SEA RES.,27A,255-264
      MILLERO AND POISSON 1981,DEEP-SEA RES.,28A PP 625-629.
      BOTH ABOVE REFERENCES ARE ALSO FOUND IN UNESCO REPORT 38 (1981)
      
      UNITS:      
      PRESSURE        P0       DECIBARS
      TEMPERATURE     T        DEG CELSIUS (IPTS-68)
      SALINITY        S        (IPSS-78)
      SPEC. VOL. ANA. SVAN     M**3/KG *1.0E-8
      DENSITY ANA.    SIGMA    KG/M**3
      ******************************************************************
      CHECK VALUE: SVAN=981.3021 E-8 M**3/KG.  FOR S = 40 (IPSS-78) ,
      T = 40 DEG C, P0= 10000 DECIBARS.
      CHECK VALUE: SIGMA = 59.82037  KG/M**3 FOR S = 40 (IPSS-78) ,
      T = 40 DEG C, P0= 10000 DECIBARS.
      CHECK VALUE: FOR S = 40 (IPSS-78) , T = 40 DEG C, P0= 10000 DECIBARS.
      DR/DP                  DR/DT                 DR/DS
      DRV(1,7)              DRV(2,3)             DRV(1,8)

      FINITE DIFFERENCE WITH 3RD ORDER CORRECTION DONE IN DOUBLE PRECSION

      3.46969238E-3       -.43311722           .705110777

      EXPLICIT DIFFERENTIATION SINGLE PRECISION FORMULATION EOS80 
 
      3.4696929E-3        -.4331173            .7051107

      (RP...I think this ---------^^^^^^ should be -.4431173!);
    */

    // *******************************************************
    using namespace std;

    // DATA
    double R3500 = 1028.1063;
    double R4 = 4.8314E-4;
    double DR350 = 28.106331;

    // CONVERT PRESSURE TO BARS AND TAKE SQUARE ROOT SALINITY.
    double P = P0 / 10.;
    //double SAL=S;
    double SR = sqrt(abs(S));
    // *********************************************************
    // PURE WATER DENSITY AT ATMOSPHERIC PRESSURE
    //   BIGG P.H.,(1967) BR. J. APPLIED PHYSICS 8 PP 521-537.

    double R1 = ((((6.536332E-9 * T - 1.120083E-6) * T + 1.001685E-4) * T - 9.095290E-3) * T +
                 6.793952E-2) *
                    T -
                28.263737;
    // SEAWATER DENSITY ATM PRESS.
    //  COEFFICIENTS INVOLVING SALINITY
    double R2 = (((5.3875E-9 * T - 8.2467E-7) * T + 7.6438E-5) * T - 4.0899E-3) * T + 8.24493E-1;
    double R3 = (-1.6546E-6 * T + 1.0227E-4) * T - 5.72466E-3;
    //  INTERNATIONAL ONE-ATMOSPHERE EQUATION OF STATE OF SEAWATER
    double SIG = (R4 * S + R3 * SR + R2) * S + R1;
    // SPECIFIC VOLUME AT ATMOSPHERIC PRESSURE
    double V350P = 1.0 / R3500;
    double SVA = -SIG * V350P / (R3500 + SIG);
    double SIGMA = SIG + DR350;
    //double V0 = 1.0/(1000.0 + SIGMA);
    //  SCALE SPECIFIC VOL. ANAMOLY TO NORMALLY REPORTED UNITS
    //    double SVAN=SVA*1.0E+8;

    // ******************************************************************
    // ******  NEW HIGH PRESSURE EQUATION OF STATE FOR SEAWATER ********
    // ******************************************************************
    //        MILLERO, ET AL , 1980 DSR 27A, PP 255-264
    //               CONSTANT NOTATION FOLLOWS ARTICLE
    // ********************************************************
    // COMPUTE COMPRESSION TERMS
    double E = (9.1697E-10 * T + 2.0816E-8) * T - 9.9348E-7;
    double BW = (5.2787E-8 * T - 6.12293E-6) * T + 3.47718E-5;
    double B = BW + E * S; // Bulk Modulus (almost)
                           //  CORRECT B FOR ANAMOLY BIAS CHANGE
                           //    double Bout = B + 5.03217E-5;

    double D = 1.91075E-4;
    double C = (-1.6078E-6 * T - 1.0981E-5) * T + 2.2838E-3;
    double AW = ((-5.77905E-7 * T + 1.16092E-4) * T + 1.43713E-3) * T - 0.1194975;
    double A = (D * SR + C) * S + AW;
    //  CORRECT A FOR ANAMOLY BIAS CHANGE
    //    double Aout = A + 3.3594055;

    double B1 = (-5.3009E-4 * T + 1.6483E-2) * T + 7.944E-2;
    double A1 = ((-6.1670E-5 * T + 1.09987E-2) * T - 0.603459) * T + 54.6746;
    double KW = (((-5.155288E-5 * T + 1.360477E-2) * T - 2.327105) * T + 148.4206) * T - 1930.06;
    double K0 = (B1 * SR + A1) * S + KW;

    // EVALUATE PRESSURE POLYNOMIAL
    // ***********************************************
    //   K EQUALS THE SECANT BULK MODULUS OF SEAWATER
    //   DK=K(S,T,P)-K(35,0,P)
    //  K35=K(35,0,P)
    // ***********************************************
    double DK = (B * P + A) * P + K0;
    double K35 = (5.03217E-5 * P + 3.359406) * P + 21582.27;
    double GAM = P / K35;
    double PK = 1.0 - GAM;
    SVA = SVA * PK + (V350P + SVA) * P * DK / (K35 * (K35 + DK));
    //  SCALE SPECIFIC VOL. ANAMOLY TO NORMALLY REPORTED UNITS
    //SVAN=SVA*1.0E+8;      // Volume anomaly
    V350P = V350P * PK;
    // ****************************************************
    // COMPUTE DENSITY ANAMOLY WITH RESPECT TO 1000.0 KG/M**3
    //  1) DR350: DENSITY ANAMOLY AT 35 (IPSS-78), 0 DEG. C AND 0 DECIBARS
    //  2) DR35P: DENSITY ANAMOLY 35 (IPSS-78), 0 DEG. C ,  PRES. VARIATION
    //  3) DVAN : DENSITY ANAMOLY VARIATIONS INVOLVING SPECFIC VOL. ANAMOLY
    // ********************************************************************
    // CHECK VALUE: SIGMA = 59.82037  KG/M**3 FOR S = 40 (IPSS-78),
    // T = 40 DEG C, P0= 10000 DECIBARS.
    // *******************************************************
    double DR35P = GAM / V350P;
    double DVAN = SVA / (V350P * (V350P + SVA));
    SIGMA = DR350 + DR35P - DVAN; // Density anomaly

    return SIGMA;
}

#endif
