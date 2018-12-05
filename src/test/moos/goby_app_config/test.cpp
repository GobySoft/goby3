// Copyright 2009-2018 Toby Schneider (http://gobysoft.org/index.wt/people/toby)
//                     GobySoft, LLC (2013-)
//                     Massachusetts Institute of Technology (2007-2014)
//
//
// This file is part of the Goby Underwater Autonomy Project Binaries
// ("The Goby Binaries").
//
// The Goby Binaries are free software: you can redistribute them and/or modify
// them under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 2 of the License, or
// (at your option) any later version.
//
// The Goby Binaries are distributed in the hope that they will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with Goby.  If not, see <http://www.gnu.org/licenses/>.

#include "config.pb.h"
#include "goby/moos/goby_moos_app.h"

class GobyMOOSAppTest : public GobyMOOSApp
{
  public:
    static GobyMOOSAppTest* get_instance();

  private:
    GobyMOOSAppTest(AppConfig& cfg) : GobyMOOSApp(&cfg)
    {
        assert(!cfg.has_submessage());
        RequestQuit();
        std::cout << "All tests passed. " << std::endl;
    }

    ~GobyMOOSAppTest() {}
    void loop() {}
    static GobyMOOSAppTest* inst_;
};

boost::shared_ptr<AppConfig> master_config;
GobyMOOSAppTest* GobyMOOSAppTest::inst_ = 0;

GobyMOOSAppTest* GobyMOOSAppTest::get_instance()
{
    if (!inst_)
    {
        master_config.reset(new AppConfig);
        inst_ = new GobyMOOSAppTest(*master_config);
    }
    return inst_;
}

int main(int argc, char* argv[]) { return goby::moos::run<GobyMOOSAppTest>(argc, argv); }
