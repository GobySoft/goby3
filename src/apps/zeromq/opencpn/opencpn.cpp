#include "goby/middleware/marshalling/protobuf.h"

#include "goby/middleware/ais.h"
#include "goby/middleware/frontseat/groups.h"
#include "goby/middleware/io/line_based/pty.h"
#include "goby/middleware/io/line_based/tcp_server.h"
#include "goby/time/convert.h"
#include "goby/time/system_clock.h"
#include "goby/util/ais.h"
#include "goby/zeromq/application/multi_thread.h"
#include "goby/zeromq/protobuf/opencpn_config.pb.h"

using goby::glog;
namespace si = boost::units::si;
using ApplicationBase =
    goby::zeromq::MultiThreadApplication<goby::apps::zeromq::protobuf::OpenCPNInterfaceConfig>;

namespace goby
{
namespace apps
{
namespace zeromq
{
class OpenCPNInterface : public ApplicationBase
{
  public:
    using TCPServerThread = goby::middleware::io::TCPServerThreadLineBased<
        goby::middleware::io::groups::nmea0183_in, goby::middleware::io::groups::nmea0183_out,
        goby::middleware::io::PubSubLayer::INTERTHREAD,
        goby::middleware::io::PubSubLayer::INTERTHREAD>;
    using PTYThread =
        goby::middleware::io::PTYThreadLineBased<goby::middleware::io::groups::nmea0183_in,
                                                 goby::middleware::io::groups::nmea0183_out,
                                                 goby::middleware::io::PubSubLayer::INTERTHREAD,
                                                 goby::middleware::io::PubSubLayer::INTERTHREAD>;

    OpenCPNInterface();

  private:
    void handle_status(const goby::middleware::frontseat::protobuf::NodeStatus& frontseat_nav);

  private:
    // vehicle name to converter
    std::map<std::string, goby::middleware::AISConverter> converters_;
    int next_mmsi_;

    goby::time::SystemClock::time_point last_ais_pos_t_{std::chrono::seconds(0)},
        last_ais_voy_t_{std::chrono::seconds(0)};
    goby::time::SystemClock::duration ais_pos_dt_, ais_voy_dt_;
};
} // namespace zeromq
} // namespace apps
} // namespace goby

int main(int argc, char* argv[])
{
    return goby::run<goby::apps::zeromq::OpenCPNInterface>(argc, argv);
}

goby::apps::zeromq::OpenCPNInterface::OpenCPNInterface()
    : next_mmsi_(cfg().mmsi_start()),
      ais_pos_dt_(goby::time::convert_duration<decltype(ais_pos_dt_)>(
          cfg().position_report_interval_with_units())),
      ais_voy_dt_(goby::time::convert_duration<decltype(ais_voy_dt_)>(
          cfg().voyage_report_interval_with_units()))
{
    interprocess().subscribe<goby::middleware::frontseat::groups::node_status>(
        [this](const goby::middleware::frontseat::protobuf::NodeStatus& frontseat_nav) {
            handle_status(frontseat_nav);
        });

    if (cfg().has_ais_server())
        launch_thread<TCPServerThread>(cfg().ais_server());
    else if (cfg().has_ais_serial())
        launch_thread<PTYThread>(cfg().ais_serial());
}

void goby::apps::zeromq::OpenCPNInterface::handle_status(
    const goby::middleware::frontseat::protobuf::NodeStatus& frontseat_nav)
{
    if (!converters_.count(frontseat_nav.name()))
        converters_.insert(
            std::make_pair(frontseat_nav.name(),
                           goby::middleware::AISConverter(next_mmsi_++, cfg().filter_length())));

    auto& converter_ = converters_.at(frontseat_nav.name());

    converter_.add_status(frontseat_nav);
    std::pair<goby::util::ais::protobuf::Position, goby::util::ais::protobuf::Voyage> ais_b_msg =
        converter_.latest_node_status_to_ais_b();

    auto now = goby::time::SystemClock::now();
    bool write_pos = (now > last_ais_pos_t_ + ais_pos_dt_);
    bool write_voy = (now > last_ais_voy_t_ + ais_voy_dt_);

    std::vector<goby::util::NMEASentence> nmeas;
    if (write_pos)
    {
        goby::util::ais::Encoder pos_encoder(ais_b_msg.first);
        nmeas = pos_encoder.as_nmea();
        last_ais_pos_t_ = now;
    }
    if (write_voy)
    {
        goby::util::ais::Encoder voy_encoder_part0(ais_b_msg.second, 0);
        goby::util::ais::Encoder voy_encoder_part1(ais_b_msg.second, 1);
        auto nmeas_0 = voy_encoder_part0.as_nmea();
        std::copy(nmeas_0.begin(), nmeas_0.end(), std::back_inserter(nmeas));
        auto nmeas_1 = voy_encoder_part1.as_nmea();
        std::copy(nmeas_1.begin(), nmeas_1.end(), std::back_inserter(nmeas));
        last_ais_voy_t_ = now;
    }

    for (auto nmea : nmeas)
    {
        glog.is_debug1() && glog << nmea.message() << std::endl;
        goby::middleware::protobuf::IOData io_data;
        io_data.set_data(nmea.message_cr_nl());
        if (cfg().has_ais_server())
            io_data.mutable_tcp_dest()->set_all_clients(true);
        interthread().publish<goby::middleware::io::groups::nmea0183_out>(io_data);
    }
}
