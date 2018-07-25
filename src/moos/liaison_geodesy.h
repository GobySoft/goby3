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

#ifndef LIAISONGEODESY20131115H
#define LIAISONGEODESY20131115H

#include <Wt/WText>
#include <Wt/WBoxLayout>
#include <Wt/WVBoxLayout>
#include <Wt/WTimer>

#include "goby/middleware/liaison_container.h"
#include "goby/moos/protobuf/liaison_config.pb.h"
#include "goby/moos/moos_geodesy.h"

namespace goby
{
    namespace common
    {
        class LiaisonGeodesy : public LiaisonContainer
        {
            
          public:
            LiaisonGeodesy(const protobuf::LiaisonConfig& cfg, Wt::WContainerWidget* parent = 0);
            
          private:

            enum LatLonMode
            {
                DECIMAL_DEGREES = 0,
                DEGREES_MINUTES = 1,
                DEGREES_MINUTES_SECONDS = 2
            };

            void loop();
            void convert_local_to_geo();
            void convert_geo_to_local();

            void reformat();

            enum LatLon { LAT, LON };
            
            std::wstring format(double angle, LatLonMode mode, LatLon type);

            void change_mode();
            
          private:
            
            const protobuf::GeodesyConfig& geodesy_config_;

            Wt::WText* datum_lat_;
            Wt::WText* datum_lon_;
            
            Wt::WPushButton* local_to_geo_button_;
            Wt::WLineEdit* local_to_geo_x_;
            Wt::WLineEdit* local_to_geo_y_;
            Wt::WText* local_to_geo_lat_;
            Wt::WText* local_to_geo_lon_;
            
            
            Wt::WPushButton* geo_to_local_button_;
            Wt::WText* geo_to_local_x_;
            Wt::WText* geo_to_local_y_;

            Wt::WLineEdit* geo_to_local_lat_deg_;
            Wt::WLineEdit* geo_to_local_lon_deg_;

            Wt::WContainerWidget* lat_min_box_;
            Wt::WLineEdit* geo_to_local_lat_min_;
            Wt::WContainerWidget* lon_min_box_;
            Wt::WLineEdit* geo_to_local_lon_min_;
            Wt::WContainerWidget* lat_hemi_box_;
            Wt::WComboBox* geo_to_local_lat_hemi_;
            Wt::WContainerWidget* lon_hemi_box_;
            Wt::WComboBox* geo_to_local_lon_hemi_;            

            Wt::WContainerWidget* lat_sec_box_;
            Wt::WLineEdit* geo_to_local_lat_sec_;
            Wt::WContainerWidget* lon_sec_box_;
            Wt::WLineEdit* geo_to_local_lon_sec_;

            Wt::WComboBox* latlon_format_;
            
            CMOOSGeodesy geodesy_;

            LatLonMode mode_;

            double last_lat_, last_lon_, last_x_, last_y_;
        };

    }
}

#endif
