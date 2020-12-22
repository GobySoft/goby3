

#include <gps.h>
#include <iostream>

namespace goby
{
namespace apps
{
namespace zeromq
{
class GPSFixData
{
  public:
    GPSFixData() : merged_mask(0) {}

    bool is_ready()
    {
        // Don't even bother to do the more detailed check if we haven't seen the
        // correct data flags in the merged data.
        if ((merged_mask & trigger_mask) == trigger_mask)
        {
            return detailed_check();
        }
        else
        {
            return false;
        }
    }

    bool detailed_check()
    {
        // Since the flags don't seem to really decide when data is new or
        // updated, we are going to check some details ourselves.
        // Right now the detailed check only checks time or latlon

        // Only if we have the LATLON set
        if (trigger_mask & LATLON_SET)
        {
            if (!std::isnan(merged_fix.latitude) && !std::isnan(merged_fix.longitude))
            {
                bool lat_lon_is_changed = (merged_fix.latitude != last_published_data.latitude) &&
                                          (merged_fix.longitude != last_published_data.longitude);

                if (lat_lon_is_changed)
                {
                    glog.is_debug2() && glog << "LATLON is changed" << std::endl;
                }
                else
                {
                    glog.is_debug2() && glog << "LATLON is NOT changed. Exiting detailed check."
                                             << std::endl;
                    return false;
                }
            }
            else
            {
                glog.is_debug2() && glog << "lat or lon is nan" << std::endl;
            }
        }

        if (trigger_mask & TIME_SET)
        {
            if (!std::isnan(merged_fix.time))
            {
                bool time_is_changed = (merged_fix.time != last_published_data.time);

                if (time_is_changed)
                {
                    glog.is_debug2() && glog << "Time is changed" << std::endl;
                }
                else
                {
                    glog.is_debug2() && glog << "TIME is NOT changed. Exiting detailed check."
                                             << std::endl;
                    return false;
                }
            }
        }
        return true;
    }

    void build_data_to_publish()
    {
        std::cout << "Name: " << name << std::endl;

        fix.set_device(name);

        if (merged_mask & TIME_SET)
        {
            fix.set_time(merged_fix.time);
        }
        using namespace goby::middleware::protobuf;
        if (merged_fix.mode == 0)
            fix.set_mode(GPSFix::ModeNotSeen);
        if (merged_fix.mode == 1)
            fix.set_mode(GPSFix::ModeNoFix);
        if (merged_fix.mode == 2)
            fix.set_mode(GPSFix::Mode2D);
        if (merged_fix.mode == 3)
            fix.set_mode(GPSFix::Mode3D);

        using namespace boost::units;

        if (merged_fix.mode >= 2 && merged_mask & LATLON_SET)
        {
            if (!isnan(merged_fix.latitude) && !isnan(merged_fix.longitude))
            {
                auto loc = fix.mutable_location();
                loc->set_lat_with_units(merged_fix.latitude * degree::degree);
                loc->set_lon(merged_fix.longitude);
            }
            else
            {
                glog.is_debug1() && glog << "Special case, lat/lon is nanish" << std::endl;
            }
        }

        if (merged_fix.mode == 3 && merged_mask & ALTITUDE_SET)
        {
            if (!isnan(merged_fix.altitude))
            {
                fix.set_altitude(merged_fix.altitude);
            }
        }

        if (merged_mask & TRACK_SET)
        {
            fix.set_track(merged_fix.track);
        }
        if (merged_mask & SPEED_SET)
        {
            fix.set_speed(merged_fix.speed);
        }
        if (merged_mask & CLIMB_SET)
        {
            fix.set_speed(merged_fix.speed);
        }

        // Each device will have a different number of fields it is going to
        // set. It feels safe to assume that when the maximum number of bits it
        // is going to set is set, it has provided evertything it is going to.
    }

    void set_data_as_published()
    {
        merged_mask = 0;
        last_published_data = merged_fix;
    }

    std::string name;

    // Mask specified in the config for fields we are looking for.
    gps_mask_t trigger_mask;
    gps_mask_t merged_mask;
    gps_fix_t merged_fix;
    gps_fix_t last_published_data;

    goby::middleware::protobuf::GPSFix fix;
};

} // namespace zeromq
} // namespace apps
} // namespace goby
