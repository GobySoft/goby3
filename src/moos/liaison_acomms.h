// Copyright 2009-2017 Toby Schneider (http://gobysoft.org/index.wt/people/toby)
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

#ifndef LIAISONACOMMS20140626H
#define LIAISONACOMMS20140626H

#include <Wt/WTimer>
#include <Wt/WPanel>
#include <Wt/WProgressBar>
#include <Wt/WCssDecorationStyle>
#include <Wt/Chart/WCartesianChart>

#include <dccl.h>

#include "goby/moos/moos_node.h"
#include "goby/common/liaison_container.h"
#include "goby/moos/protobuf/liaison_config.pb.h"
#include "goby/moos/protobuf/pAcommsHandler_config.pb.h"

namespace goby
{
    namespace common
    {
        class QueueBar : public Wt::WProgressBar
        {
          public:
            QueueBar(Wt::WContainerWidget* parent = 0)
                : Wt::WProgressBar(parent)
            { }
            Wt::WString text() const;            
            
        };
        
        class MACBar : public Wt::WProgressBar
        {
          public:
            MACBar(Wt::WContainerWidget* parent = 0)
                : Wt::WProgressBar(parent)
            { }
            Wt::WString text() const;            
            
        };

        
        class LiaisonAcomms : public LiaisonContainer, public goby::moos::MOOSNode
        {
            
          public:
            LiaisonAcomms(ZeroMQService* zeromq_service, const protobuf::LiaisonConfig& cfg, Wt::WContainerWidget* parent = 0);
            
          private:
            void process_acomms_config();
            
            void loop();
            void moos_inbox(CMOOSMsg& msg);
            
            void dccl_analyze(const Wt::WMouseEvent& event);
            void dccl_select(Wt::WString msg); 
            void dccl_message(const Wt::WMouseEvent& event);

            void queue_info(const Wt::WMouseEvent& event, int id);
            void queue_flush(const Wt::WMouseEvent& event, int id);

            void mac_info(const Wt::WMouseEvent& event, int id);

            class DriverStats;
            void update_driver_stats(int now, DriverStats* driver_stats);
            void handle_modem_message(DriverStats* driver_stats, bool good, goby::acomms::protobuf::ModemTransmission& msg);

            void driver_info(const Wt::WMouseEvent& event, DriverStats* driver_stats);
            void mm_check(int axis, int column, bool checked);
            void mm_range(double range);  
            
            void focus()
            { timer_.start(); }

            void unfocus()
            { timer_.stop(); }


            std::string format_seconds(int sec);
            
          private:
            boost::mutex dccl_mutex_;
            dccl::Codec dccl_;
            
            Wt::WPushButton* dccl_analyze_;
            Wt::WPushButton* dccl_message_;
            Wt::WText* dccl_analyze_text_;
            Wt::WText* dccl_message_text_;
            Wt::WComboBox* dccl_combo_;
            Wt::WTable* queue_table_;

            Wt::WContainerWidget* amac_box_;
            
            std::map<dccl::int32, QueueBar*> queue_bars_;

            // maps dccl id onto QueuedMessageEntry index
            std::map<dccl::int32, int> queue_cfg_;

            // maps index of MAC cycle to container
            std::map<int, Wt::WContainerWidget*> mac_slots_;
            std::map<int, MACBar*> mac_bars_;
            MACBar* mac_cycle_bar_;
            Wt::WCssDecorationStyle mac_slot_style_;
            
            struct QueueStats
            {
            QueueStats() : last_rx_time(-1) { }
                
                int last_rx_time;
                Wt::WText* last_rx_time_text;
            };
            std::map<dccl::int32, QueueStats> queue_stats_;
            
            ZeroMQService* zeromq_service_;
            const protobuf::AcommsConfig& cfg_;
            pAcommsHandlerConfig acomms_config_;
            bool have_acomms_config_;

            Wt::WPanel* driver_panel_;
            WContainerWidget* driver_box_;
            
            enum Direction { RX, TX };
            
            struct DriverStats
            {
            DriverStats(Direction d): direction(d), last_time(-1) 
                    { }
                
                Direction direction;
                int last_time;
                Wt::WText* last_time_text;
                goby::acomms::protobuf::ModemTransmission last_msg_;
                Wt::WGroupBox* box;
            };

            DriverStats driver_rx_;
            DriverStats driver_tx_;

            Wt::WGroupBox* mm_rx_stats_box_;
            Wt::WStandardItemModel* mm_rx_stats_model_;
            Wt::Chart::WCartesianChart* mm_rx_stats_graph_;

            enum { TIME_COLUMN = 0, ELAPSED_COLUMN = 1, MSE_COLUMN = 2, SNR_IN_COLUMN = 3, SNR_OUT_COLUMN = 4, DOPPLER_COLUMN = 5, PERCENT_BAD_FRAMES_COLUMN = 6, MAX_COLUMN = 6};
            
            int mm_rx_stats_range_;

            // column -> axis -> Checkbox 
            std::map<int, std::map<int, Wt::WCheckBox*> > mm_rx_chks_;
            
            Wt::WTimer timer_;

            goby::acomms::protobuf::ModemTransmission last_slot_;
            
            
        };

    }
}

#endif
