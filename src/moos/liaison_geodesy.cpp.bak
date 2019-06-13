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

#include <Wt/WBreak>
#include <Wt/WComboBox>
#include <Wt/WContainerWidget>
#include <Wt/WGroupBox>
#include <Wt/WLineEdit>
#include <Wt/WPushButton>

#include <goby/util/as.h>

#include "liaison_geodesy.h"

using namespace Wt;

goby::moos::LiaisonGeodesy::LiaisonGeodesy(const protobuf::LiaisonConfig& cfg,
                                           Wt::WContainerWidget* parent)
    : LiaisonContainer(parent),
      geodesy_config_(cfg.GetExtension(protobuf::geodesy_config)),
      mode_(DECIMAL_DEGREES),
      last_lat_(std::numeric_limits<double>::quiet_NaN()),
      last_lon_(std::numeric_limits<double>::quiet_NaN()),
      last_x_(std::numeric_limits<double>::quiet_NaN()),
      last_y_(std::numeric_limits<double>::quiet_NaN())
{
    latlon_format_ = new Wt::WComboBox(this);
    latlon_format_->addItem("Decimal Degrees");
    latlon_format_->addItem("Degrees, Decimal Minutes");
    latlon_format_->addItem("Degrees, Minutes, Seconds");

    latlon_format_->changed().connect(this, &LiaisonGeodesy::change_mode);

    WGroupBox* datum_box = new Wt::WGroupBox("Datum", this);
    new WText("Latitude: ", datum_box);
    datum_lat_ = new WText(datum_box);
    new WBreak(datum_box);
    new WText("Longitude: ", datum_box);
    datum_lon_ = new WText(datum_box);

    geodesy_.Initialise(geodesy_config_.lat_origin(), geodesy_config_.lon_origin());

    WGroupBox* local_to_geo_box = new Wt::WGroupBox("Local UTM to Latitude/Longitude", this);

    new WText("X (m): ", local_to_geo_box);
    local_to_geo_x_ = new WLineEdit(local_to_geo_box);

    new WBreak(local_to_geo_box);
    new WText("Y (m): ", local_to_geo_box);
    local_to_geo_y_ = new WLineEdit(local_to_geo_box);
    new WBreak(local_to_geo_box);
    local_to_geo_button_ = new WPushButton("Convert", local_to_geo_box);
    local_to_geo_button_->clicked().connect(this, &LiaisonGeodesy::convert_local_to_geo);
    local_to_geo_x_->changed().connect(this, &LiaisonGeodesy::convert_local_to_geo);
    local_to_geo_y_->changed().connect(this, &LiaisonGeodesy::convert_local_to_geo);

    new WBreak(local_to_geo_box);

    new WText("Latitude: ", local_to_geo_box);
    local_to_geo_lat_ = new WText(local_to_geo_box);
    new WBreak(local_to_geo_box);
    new WText("Longitude: ", local_to_geo_box);
    local_to_geo_lon_ = new WText(local_to_geo_box);

    WGroupBox* geo_to_local_box = new Wt::WGroupBox("Latitude/Longitude to Local UTM", this);

    WContainerWidget* lat_box = new WContainerWidget(geo_to_local_box);
    new WText("Latitude: ", lat_box);
    lat_box->resize(WLength(20, Wt::WLength::FontEm), WLength());

    geo_to_local_lat_deg_ = new WLineEdit(geo_to_local_box);
    new WText(std::wstring(1, wchar_t(0x00B0)), geo_to_local_box);
    geo_to_local_lat_deg_->resize(WLength(5, Wt::WLength::FontEm), WLength());
    lat_min_box_ = new WContainerWidget(geo_to_local_box);
    lat_min_box_->setInline(true);
    geo_to_local_lat_min_ = new WLineEdit(lat_min_box_);
    new WText("'", lat_min_box_);
    geo_to_local_lat_min_->resize(WLength(5, Wt::WLength::FontEm), WLength());
    lat_sec_box_ = new WContainerWidget(geo_to_local_box);
    lat_sec_box_->setInline(true);
    geo_to_local_lat_sec_ = new WLineEdit(lat_sec_box_);
    new WText("\"", lat_sec_box_);
    geo_to_local_lat_sec_->resize(WLength(5, Wt::WLength::FontEm), WLength());
    lat_hemi_box_ = new WContainerWidget(geo_to_local_box);
    lat_hemi_box_->setInline(true);
    geo_to_local_lat_hemi_ = new Wt::WComboBox(lat_hemi_box_);
    geo_to_local_lat_hemi_->addItem("N");
    geo_to_local_lat_hemi_->addItem("S");

    new WBreak(geo_to_local_box);

    WContainerWidget* lon_box = new WContainerWidget(geo_to_local_box);
    new WText("Longitude: ", lon_box);
    lon_box->resize(WLength(20, Wt::WLength::FontEm), WLength());

    geo_to_local_lon_deg_ = new WLineEdit(geo_to_local_box);
    geo_to_local_lon_deg_->resize(WLength(5, Wt::WLength::FontEm), WLength());
    new WText(std::wstring(1, wchar_t(0x00B0)), geo_to_local_box);
    lon_min_box_ = new WContainerWidget(geo_to_local_box);
    lon_min_box_->setInline(true);
    geo_to_local_lon_min_ = new WLineEdit(lon_min_box_);
    new WText("'", lon_min_box_);
    geo_to_local_lon_min_->resize(WLength(5, Wt::WLength::FontEm), WLength());
    lon_sec_box_ = new WContainerWidget(geo_to_local_box);
    lon_sec_box_->setInline(true);
    geo_to_local_lon_sec_ = new WLineEdit(lon_sec_box_);
    new WText("'", lon_sec_box_);
    geo_to_local_lon_sec_->resize(WLength(5, Wt::WLength::FontEm), WLength());
    lon_hemi_box_ = new WContainerWidget(geo_to_local_box);
    lon_hemi_box_->setInline(true);
    geo_to_local_lon_hemi_ = new Wt::WComboBox(lon_hemi_box_);
    geo_to_local_lon_hemi_->addItem("E");
    geo_to_local_lon_hemi_->addItem("W");

    new WBreak(geo_to_local_box);

    geo_to_local_button_ = new WPushButton("Convert", geo_to_local_box);
    new WBreak(geo_to_local_box);

    new WText("X (m): ", geo_to_local_box);
    geo_to_local_x_ = new WText(geo_to_local_box);
    new WBreak(geo_to_local_box);
    new WText("Y (m): ", geo_to_local_box);
    geo_to_local_y_ = new WText(geo_to_local_box);

    geo_to_local_button_->clicked().connect(this, &LiaisonGeodesy::convert_geo_to_local);
    geo_to_local_lat_deg_->changed().connect(this, &LiaisonGeodesy::convert_geo_to_local);
    geo_to_local_lat_min_->changed().connect(this, &LiaisonGeodesy::convert_geo_to_local);
    geo_to_local_lat_sec_->changed().connect(this, &LiaisonGeodesy::convert_geo_to_local);
    geo_to_local_lat_hemi_->changed().connect(this, &LiaisonGeodesy::convert_geo_to_local);
    geo_to_local_lon_deg_->changed().connect(this, &LiaisonGeodesy::convert_geo_to_local);
    geo_to_local_lon_min_->changed().connect(this, &LiaisonGeodesy::convert_geo_to_local);
    geo_to_local_lon_sec_->changed().connect(this, &LiaisonGeodesy::convert_geo_to_local);
    geo_to_local_lon_hemi_->changed().connect(this, &LiaisonGeodesy::convert_geo_to_local);

    change_mode();

    set_name("MOOSGeodesy");
}

void goby::moos::LiaisonGeodesy::loop() {}

void goby::moos::LiaisonGeodesy::convert_local_to_geo()
{
    geodesy_.UTM2LatLong(goby::util::as<double>(boost::trim_copy(local_to_geo_x_->text().narrow())),
                         goby::util::as<double>(boost::trim_copy(local_to_geo_y_->text().narrow())),
                         last_lat_, last_lon_);
    reformat();
}

void goby::moos::LiaisonGeodesy::convert_geo_to_local()
{
    double lat_deg =
               goby::util::as<double>(boost::trim_copy(geo_to_local_lat_deg_->text().narrow())),
           lat_min =
               goby::util::as<double>(boost::trim_copy(geo_to_local_lat_min_->text().narrow())),
           lat_sec =
               goby::util::as<double>(boost::trim_copy(geo_to_local_lat_sec_->text().narrow()));

    double lon_deg =
               goby::util::as<double>(boost::trim_copy(geo_to_local_lon_deg_->text().narrow())),
           lon_min =
               goby::util::as<double>(boost::trim_copy(geo_to_local_lon_min_->text().narrow())),
           lon_sec =
               goby::util::as<double>(boost::trim_copy(geo_to_local_lon_sec_->text().narrow()));

    double lat = lat_deg;
    double lon = lon_deg;
    switch (mode_)
    {
        case DEGREES_MINUTES_SECONDS:
            lat += lat > 0 ? lat_sec / 3600 : -lat_sec / 3600;
            lon += lon > 0 ? lon_sec / 3600 : -lon_sec / 3600;
            // fall through intentional
        case DEGREES_MINUTES:
            lat += lat > 0 ? lat_min / 60 : -lat_min / 60;
            if (geo_to_local_lat_hemi_->currentIndex() == 1)
                lat = -lat;
            lon += lon > 0 ? lon_min / 60 : -lon_min / 60;
            if (geo_to_local_lon_hemi_->currentIndex() == 1)
                lon = -lon;
            // fall through intentional
        default:
        case DECIMAL_DEGREES: break;
    }

    geodesy_.LatLong2LocalUTM(lat, lon, last_y_, last_x_);

    if (!std::isnan(last_x_) && !std::isnan(last_y_))
    {
        std::stringstream x_ss, y_ss;
        x_ss << std::fixed << std::setprecision(2) << last_x_;
        y_ss << std::fixed << std::setprecision(2) << last_y_;

        geo_to_local_x_->setText(x_ss.str());
        geo_to_local_y_->setText(y_ss.str());
    }
    else
    {
        geo_to_local_x_->setText("---");
        geo_to_local_y_->setText("---");
    }
}

void goby::moos::LiaisonGeodesy::change_mode()
{
    int mode = latlon_format_->currentIndex();
    mode_ = static_cast<LatLonMode>(mode);
    reformat();

    switch (mode_)
    {
        default:
        case DECIMAL_DEGREES:
        {
            geo_to_local_lat_deg_->resize(WLength(20, Wt::WLength::FontEm), WLength());

            geo_to_local_lon_deg_->resize(WLength(20, Wt::WLength::FontEm), WLength());

            lat_min_box_->hide();
            lat_hemi_box_->hide();
            lon_min_box_->hide();
            lon_hemi_box_->hide();
            lon_sec_box_->hide();
            lat_sec_box_->hide();
            break;
        }
        case DEGREES_MINUTES:
        {
            geo_to_local_lat_deg_->resize(WLength(5, Wt::WLength::FontEm), WLength());

            geo_to_local_lon_deg_->resize(WLength(5, Wt::WLength::FontEm), WLength());
            geo_to_local_lat_min_->resize(WLength(15, Wt::WLength::FontEm), WLength());

            geo_to_local_lon_min_->resize(WLength(15, Wt::WLength::FontEm), WLength());

            lat_sec_box_->hide();
            lon_sec_box_->hide();
            lat_min_box_->show();
            lat_hemi_box_->show();
            lon_min_box_->show();
            lon_hemi_box_->show();
            break;
        }
        case DEGREES_MINUTES_SECONDS:
        {
            geo_to_local_lat_deg_->resize(WLength(5, Wt::WLength::FontEm), WLength());

            geo_to_local_lon_deg_->resize(WLength(5, Wt::WLength::FontEm), WLength());
            geo_to_local_lat_min_->resize(WLength(5, Wt::WLength::FontEm), WLength());

            geo_to_local_lon_min_->resize(WLength(5, Wt::WLength::FontEm), WLength());

            lat_sec_box_->show();
            lon_sec_box_->show();
            lat_min_box_->show();
            lat_hemi_box_->show();
            lon_min_box_->show();
            lon_hemi_box_->show();
            break;
        }
    }
    convert_geo_to_local();
}

void goby::moos::LiaisonGeodesy::reformat()
{
    datum_lat_->setText(format(geodesy_config_.lat_origin(), mode_, LAT));
    datum_lon_->setText(format(geodesy_config_.lon_origin(), mode_, LON));

    if (!std::isnan(last_lat_))
        local_to_geo_lat_->setText(format(last_lat_, mode_, LAT));
    else
        local_to_geo_lat_->setText("---");

    if (!std::isnan(last_lon_))
        local_to_geo_lon_->setText(format(last_lon_, mode_, LON));
    else
        local_to_geo_lon_->setText("---");
}

std::wstring goby::moos::LiaisonGeodesy::format(double angle, LatLonMode mode, LatLon type)
{
    char pos = (type == LAT) ? 'N' : 'E';
    char neg = (type == LAT) ? 'S' : 'W';

    switch (mode_)
    {
        default:
        case DECIMAL_DEGREES:
        {
            std::wstringstream ss;
            ss << std::fixed << std::setprecision(7) << angle << wchar_t(0x00B0);
            return ss.str();
        }

        case DEGREES_MINUTES:
        {
            int deg = std::abs(angle);
            double min = (std::abs(angle) - deg) * 60;
            std::wstringstream ss;
            ss << deg << wchar_t(0x00B0) << " " << std::setprecision(7) << min << "' "
               << ((angle > 0) ? pos : neg);
            return ss.str();
        }
        case DEGREES_MINUTES_SECONDS:
        {
            int deg = std::abs(angle);
            int min = (std::abs(angle) - deg) * 60;
            double sec = (std::abs(angle) - deg - min / 60.0) * 3600;
            std::wstringstream ss;
            ss << deg << wchar_t(0x00B0) << " " << min << "' " << sec << "\" "
               << ((angle > 0) ? pos : neg);
            return ss.str();
        }
    }
}
