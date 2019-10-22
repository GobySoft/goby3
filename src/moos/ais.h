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

#ifndef MOOSAIS201910122H
#define MOOSAIS201910122H

#include <numeric>

#include <boost/algorithm/string.hpp>
#include <boost/circular_buffer.hpp>

#include "goby/moos/protobuf/node_status.pb.h"
#include "goby/util/protobuf/ais.pb.h"

namespace goby
{
namespace moos
{
class AISConverter
{
  public:
    AISConverter(int mmsi, int history_length = 2) : mmsi_(mmsi), status_reports_(history_length)
    {
        if (history_length < 2)
            throw(std::runtime_error("History length must be >= 2"));
    }

    void add_status(const goby::moos::protobuf::NodeStatus& status)
    {
        // reject duplications
        if (status_reports_.empty() ||
            status.SerializeAsString() != status_reports_.back().SerializeAsString())
            status_reports_.push_back(status);
    }

    bool empty() { return status_reports_.empty(); }

    std::pair<goby::util::ais::protobuf::Position, goby::util::ais::protobuf::Voyage>
    latest_node_status_to_ais_b()
    {
        using namespace boost::units;
        using boost::units::quantity;
        using goby::util::ais::protobuf::Position;
        using goby::util::ais::protobuf::Voyage;

        if (status_reports_.size() == 0)
            throw(std::runtime_error("No status reports"));

        const goby::moos::protobuf::NodeStatus& status = status_reports_.back();

        Position pos;
        pos.set_message_id(18); // Class B position report
        pos.set_mmsi(mmsi_);
        pos.set_nav_status(goby::util::ais::protobuf::AIS_STATUS__UNDER_WAY_USING_ENGINE);
        if (status.global_fix().has_lat())
            pos.set_lat_with_units(status.global_fix().lat_with_units());
        if (status.global_fix().has_lon())
            pos.set_lon_with_units(status.global_fix().lon_with_units());
        if (status.pose().has_heading())
            pos.set_true_heading_with_units(status.pose().heading_with_units());

        std::vector<quantity<si::velocity>> sogs;
        std::vector<quantity<si::plane_angle>> cogs;

        for (int i = 1, n = status_reports_.size(); i < n; ++i)
        {
            auto& status0 = status_reports_[i - 1];
            auto& status1 = status_reports_[i];

            auto dy = status1.local_fix().y_with_units() - status0.local_fix().y_with_units();
            auto dx = status1.local_fix().x_with_units() - status0.local_fix().x_with_units();
            auto dt = status1.time_with_units() - status0.time_with_units();

            auto ninety_degrees(90. * boost::units::degree::degrees);
            decltype(ninety_degrees) cog_angle(boost::units::atan2(dy, dx));

            sogs.push_back(boost::units::sqrt(dy * dy + dx * dx) / dt);
            cogs.push_back(quantity<si::plane_angle>(ninety_degrees - cog_angle));
        }
        auto sog_sum =
            std::accumulate(sogs.begin(), sogs.end(), 0. * boost::units::si::meters_per_second);

        auto cog_sum = std::accumulate(cogs.begin(), cogs.end(), 0. * boost::units::si::radians);

        pos.set_speed_over_ground_with_units(sog_sum / quantity<si::dimensionless>(sogs.size()));
        pos.set_course_over_ground_with_units(cog_sum / quantity<si::dimensionless>(cogs.size()));

        Voyage voy;
        voy.set_message_id(24); // Class B voyage
        voy.set_mmsi(mmsi_);
        voy.set_name(boost::to_upper_copy(status.name()));
        voy.set_type(Voyage::TYPE__OTHER);

        return std::make_pair(pos, voy);
    }

  private:
    int mmsi_;
    boost::circular_buffer<goby::moos::protobuf::NodeStatus> status_reports_;
}; // namespace moos

} // namespace moos
} // namespace goby

#endif
