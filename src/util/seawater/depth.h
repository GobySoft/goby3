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

#ifndef DEPTHH
#define DEPTHH

#include <cmath>


// Calculates Depth given the Pressure at some Latitude.
// Taken directly from MATLAB OceansToolbox depth.m

inline double pressure2depth(double P, double LAT)
{
  // function DEPTH=depth(P,LAT);
  /*
    DEPTH   Computes depth given the pressure at some latitude
    D=DEPTH(P,LAT) gives the depth D (m) for a pressure P (dbars)
    at some latitude LAT (degrees).
    
    This probably works best in mid-latiude oceans, if anywhere!

    Ref: Saunders, Fofonoff, Deep Sea Res., 23 (1976), 109-111
    

    Notes: RP (WHOI) 2/Dec/91
    I copied this directly from the UNESCO algorithms

    CHECKVALUE: DEPTH = 9712.653 M FOR P=10000 DECIBARS, LATITUDE=30 DEG
    ABOVE FOR STANDARD OCEAN: T=0 DEG. CELSUIS ; S=35 (IPSS-78)
  */
    
    using namespace std; // for math functions
        
    double X = sin(LAT/57.29578);
    X = X*X;
    // GR= GRAVITY VARIATION WITH LATITUDE: ANON (1970) BULLETIN GEODESIQUE
    double GR = 9.780318*(1.0+(5.2788E-3+2.36E-5*X)*X) + 1.092E-6*P;
    double DEPTH = (((-1.82E-15*P+2.279E-10)*P-2.2512E-5)*P+9.72659)*P;
    DEPTH=DEPTH/GR;

    return DEPTH;
}

#endif
