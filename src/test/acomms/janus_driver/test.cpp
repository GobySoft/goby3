#include "../driver_tester/driver_tester.h"
#include "goby/acomms/modemdriver/janus_driver.h"
#include <cstdlib>


std::shared_ptr<goby::acomms::ModemDriverBase> driver1, driver2;

void handle_raw_incoming(int driver, const goby::acomms::protobuf::ModemRaw& raw)
{
    std::cout << "Raw in (" << driver << "): " << raw.ShortDebugString() << std::endl;
}

void handle_raw_outgoing(int driver, const goby::acomms::protobuf::ModemRaw& raw)
{
    std::cout << "Raw out (" << driver << "): " << raw.ShortDebugString() << std::endl;
}

int main(int argc, char* argv[])
{
    goby::glog.add_stream(goby::util::logger::DEBUG3, &std::clog);
    std::ofstream fout;

    if (argc == 2)
    {
        fout.open(argv[1]);
        goby::glog.add_stream(goby::util::logger::DEBUG3, &fout);
    }

    goby::glog.set_name(argv[0]);

    driver1.reset(new goby::acomms::JanusDriver);
    driver2.reset(new goby::acomms::JanusDriver);

    goby::acomms::connect(&driver1->signal_raw_incoming,
                          std::bind(&handle_raw_incoming, 1, std::placeholders::_1));
    goby::acomms::connect(&driver2->signal_raw_incoming,
                          std::bind(&handle_raw_incoming, 2, std::placeholders::_1));
    goby::acomms::connect(&driver1->signal_raw_outgoing,
                          std::bind(&handle_raw_outgoing, 1, std::placeholders::_1));
    goby::acomms::connect(&driver2->signal_raw_outgoing,
                          std::bind(&handle_raw_outgoing, 2, std::placeholders::_1));

    goby::acomms::protobuf::DriverConfig cfg1, cfg2;

    cfg1.set_modem_id(1);
    cfg2.set_modem_id(2);

    std::vector<int> tests_to_run;
    tests_to_run.push_back(4);
    tests_to_run.push_back(5);

    goby::test::acomms::DriverTester tester(driver1, driver2, cfg1, cfg2, tests_to_run,
                                            goby::acomms::protobuf::DRIVER_JANUS);
    return tester.run();
}
