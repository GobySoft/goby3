#include "goby/middleware/marshalling/protobuf.h"

#include "goby/middleware/application/multi_thread.h"
#include "goby/middleware/io/line_based/pty.h"
#include "goby/middleware/io/line_based/serial.h"

#include "config.pb.h"

namespace goby
{
namespace apps
{
namespace middleware
{
namespace groups
{
constexpr goby::middleware::Group serial_primary_in{"serial_primary_in"};
constexpr goby::middleware::Group serial_primary_out{"serial_primary_out"};

constexpr goby::middleware::Group pty_secondary_in{"pty_secondary_in"};

} // namespace groups

class SerialMux
    : public goby::middleware::MultiThreadStandaloneApplication<protobuf::SerialMuxConfig>
{
  public:
    SerialMux()
    {
        // input to primary serial -> directly to output of pty
        using SerialThread = goby::middleware::io::SerialThreadLineBased<
            groups::serial_primary_in, groups::serial_primary_out,
            goby::middleware::io::PubSubLayer::INTERTHREAD,
            goby::middleware::io::PubSubLayer::INTERTHREAD>;
        using PTYThread = goby::middleware::io::PTYThreadLineBased<
            groups::pty_secondary_in, groups::serial_primary_in,
            goby::middleware::io::PubSubLayer::INTERTHREAD,
            goby::middleware::io::PubSubLayer::INTERTHREAD>;

        interthread().subscribe<groups::pty_secondary_in>(
            [this](std::shared_ptr<const goby::middleware::protobuf::IOData> from_pty) {
                if (allow_write_.count(from_pty->index()))
                {
                    auto to_serial =
                        std::make_shared<goby::middleware::protobuf::IOData>(*from_pty);
                    to_serial->clear_index();
                    interthread().publish<groups::serial_primary_out>(to_serial);
                }
            });

        launch_thread<SerialThread>(cfg().primary_serial());

        int pty_index = 0;
        for (const auto& secondary_cfg : cfg().secondary())
        {
            if (secondary_cfg.allow_write())
                allow_write_.insert(pty_index);
            launch_thread<PTYThread>(pty_index++, secondary_cfg.pty());
        }
    }

  private:
    std::set<int> allow_write_;
};

} // namespace middleware
} // namespace apps
} // namespace goby

int main(int argc, char* argv[])
{
    return goby::run<goby::apps::middleware::SerialMux>(argc, argv);
}
