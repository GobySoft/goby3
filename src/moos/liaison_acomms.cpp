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

#include <numeric>
#include <sstream>

#include <Wt/Chart/WCartesianChart>
#include <Wt/Chart/WDataSeries>
#include <Wt/WBreak>
#include <Wt/WCheckBox>
#include <Wt/WColor>
#include <Wt/WComboBox>
#include <Wt/WContainerWidget>
#include <Wt/WDialog>
#include <Wt/WDoubleSpinBox>
#include <Wt/WFont>
#include <Wt/WGroupBox>
#include <Wt/WLabel>
#include <Wt/WLineEdit>
#include <Wt/WPanel>
#include <Wt/WPushButton>
#include <Wt/WStandardItem>
#include <Wt/WStandardItemModel>
#include <Wt/WTable>

#include "goby/util/as.h"
#include "goby/util/dynamic_protobuf_manager.h"

#include "goby/acomms/protobuf/mm_driver.pb.h"
#include "goby/moos/moos_protobuf_helpers.h"

#include "liaison_acomms.h"

const std::string STRIPE_ODD_CLASS = "odd";
const std::string STRIPE_EVEN_CLASS = "even";

using namespace goby::common::logger;
using namespace Wt;

goby::common::LiaisonAcomms::LiaisonAcomms(ZeroMQService* zeromq_service,
                                           const protobuf::LiaisonConfig& cfg,
                                           Wt::WContainerWidget* parent)
    : LiaisonContainer(parent), MOOSNode(zeromq_service), zeromq_service_(zeromq_service),
      cfg_(cfg.GetExtension(protobuf::acomms_config)), have_acomms_config_(false), driver_rx_(RX),
      driver_tx_(TX), mm_rx_stats_box_(0), mm_rx_stats_range_(600)
{
    for (int i = 0, n = cfg.load_shared_library_size(); i < n; ++i)
        dccl_.load_library(cfg.load_shared_library(i));

    protobuf::ZeroMQServiceConfig ipc_sockets;
    protobuf::ZeroMQServiceConfig::Socket* internal_subscribe_socket = ipc_sockets.add_socket();
    internal_subscribe_socket->set_socket_type(protobuf::ZeroMQServiceConfig::Socket::SUBSCRIBE);
    internal_subscribe_socket->set_socket_id(LIAISON_INTERNAL_SUBSCRIBE_SOCKET);
    internal_subscribe_socket->set_transport(protobuf::ZeroMQServiceConfig::Socket::INPROC);
    internal_subscribe_socket->set_connect_or_bind(protobuf::ZeroMQServiceConfig::Socket::CONNECT);
    internal_subscribe_socket->set_socket_name(liaison_internal_publish_socket_name());

    protobuf::ZeroMQServiceConfig::Socket* internal_publish_socket = ipc_sockets.add_socket();
    internal_publish_socket->set_socket_type(protobuf::ZeroMQServiceConfig::Socket::PUBLISH);
    internal_publish_socket->set_socket_id(LIAISON_INTERNAL_PUBLISH_SOCKET);
    internal_publish_socket->set_transport(protobuf::ZeroMQServiceConfig::Socket::INPROC);
    internal_publish_socket->set_connect_or_bind(protobuf::ZeroMQServiceConfig::Socket::CONNECT);
    internal_publish_socket->set_socket_name(liaison_internal_subscribe_socket_name());

    zeromq_service_->merge_cfg(ipc_sockets);

    subscribe("ACOMMS_CONFIG", LIAISON_INTERNAL_SUBSCRIBE_SOCKET);
    subscribe("ACOMMS_QSIZE", LIAISON_INTERNAL_SUBSCRIBE_SOCKET);
    subscribe("ACOMMS_QUEUE_RECEIVE", LIAISON_INTERNAL_SUBSCRIBE_SOCKET);
    subscribe("ACOMMS_MAC_SLOT_START", LIAISON_INTERNAL_SUBSCRIBE_SOCKET);
    subscribe("ACOMMS_MODEM_TRANSMIT", LIAISON_INTERNAL_SUBSCRIBE_SOCKET);
    subscribe("ACOMMS_MODEM_RECEIVE", LIAISON_INTERNAL_SUBSCRIBE_SOCKET);

    MOOSNode::send(CMOOSMsg(MOOS_NOTIFY, "ACOMMS_CONFIG_REQUEST", "Request"),
                   LIAISON_INTERNAL_PUBLISH_SOCKET);

    timer_.setInterval(1 / cfg.update_freq() * 1.0e3);
    timer_.timeout().connect(this, &LiaisonAcomms::loop);

    WPanel* dccl_panel = new Wt::WPanel(this);
    dccl_panel->setTitle("DCCL");
    dccl_panel->setCollapsible(true);
    dccl_panel->setCollapsed(cfg_.minimize_dccl());

    WContainerWidget* dccl_box = new Wt::WContainerWidget();
    dccl_panel->setCentralWidget(dccl_box);
    new WLabel("Message: ", dccl_box);
    dccl_combo_ = new WComboBox(dccl_box);

    dccl_combo_->addItem("(Choose a message)");

    dccl_combo_->sactivated().connect(this, &LiaisonAcomms::dccl_select);

    dccl_message_ = new WPushButton("Show .proto", dccl_box);
    dccl_message_->clicked().connect(this, &LiaisonAcomms::dccl_message);

    dccl_analyze_ = new WPushButton("Analyze", dccl_box);
    dccl_analyze_->clicked().connect(this, &LiaisonAcomms::dccl_analyze);

    new WBreak(dccl_box);

    dccl_message_text_ = new WText("", Wt::PlainText, dccl_box);
    WFont mono(Wt::WFont::Monospace);
    dccl_message_text_->decorationStyle().setFont(mono);
    dccl_message_text_->hide();

    new WBreak(dccl_box);

    dccl_analyze_text_ = new WText("", Wt::PlainText, dccl_box);
    dccl_analyze_text_->decorationStyle().setFont(mono);
    dccl_analyze_text_->hide();

    WPanel* queue_panel = new Wt::WPanel(this);
    queue_panel->setTitle("Queue");
    queue_panel->setCollapsible(true);
    WContainerWidget* queue_box = new Wt::WContainerWidget();
    queue_panel->setCentralWidget(queue_box);
    queue_panel->setCollapsed(cfg_.minimize_queue());

    queue_table_ = new Wt::WTable(queue_box);
    queue_table_->setStyleClass("basic_small");
    queue_table_->setHeaderCount(1);
    queue_table_->elementAt(0, 0)->addWidget(new WText("DCCL Message"));
    queue_table_->elementAt(0, 1)->addWidget(new WText("Queue Size"));
    queue_table_->elementAt(0, 2)->addWidget(new WText("Time since Receive"));

    WPanel* amac_panel = new Wt::WPanel(this);
    amac_panel->setTitle("AMAC");
    amac_panel->setCollapsible(true);
    amac_box_ = new Wt::WContainerWidget();
    amac_panel->setCentralWidget(amac_box_);
    amac_panel->setCollapsed(cfg_.minimize_amac());

    last_slot_.set_slot_index(-1);

    driver_panel_ = new Wt::WPanel(this);
    driver_panel_->setTitle("Driver");
    driver_panel_->setCollapsible(true);
    driver_box_ = new Wt::WContainerWidget();
    driver_panel_->setCentralWidget(driver_box_);
    driver_panel_->setCollapsed(cfg_.minimize_driver());

    driver_tx_.box = new Wt::WGroupBox("Transmit", driver_box_);
    driver_tx_.last_time_text = new Wt::WText(driver_tx_.box);
    driver_tx_.box->clicked().connect(
        boost::bind(&LiaisonAcomms::driver_info, this, _1, &driver_tx_));

    driver_rx_.box = new Wt::WGroupBox("Receive", driver_box_);
    driver_rx_.last_time_text = new Wt::WText(driver_rx_.box);
    driver_rx_.box->clicked().connect(
        boost::bind(&LiaisonAcomms::driver_info, this, _1, &driver_rx_));

    set_name("MOOSAcomms");
}

void goby::common::LiaisonAcomms::loop()
{
    while (zeromq_service_->poll(0)) {}

    int now = goby::common::goby_time<double>();
    for (std::map<dccl::int32, QueueStats>::const_iterator it = queue_stats_.begin(),
                                                           end = queue_stats_.end();
         it != end; ++it)
    {
        if (it->second.last_rx_time > 0)
            it->second.last_rx_time_text->setText(
                "Last: " + format_seconds(now - it->second.last_rx_time) + " ago");
        else
            it->second.last_rx_time_text->setText("Never");
    }

    if (mac_bars_.count(last_slot_.slot_index()))
    {
        int seconds_into_slot = now - static_cast<int>(last_slot_.time() / 1e6);
        mac_bars_[last_slot_.slot_index()]->setValue(seconds_into_slot);

        int seconds_into_cycle = seconds_into_slot;
        for (int i = 0, n = last_slot_.slot_index(); i < n; ++i)
            seconds_into_cycle += acomms_config_.mac_cfg().slot(i).slot_seconds();
        mac_cycle_bar_->setValue(seconds_into_cycle);
    }

    update_driver_stats(now, &driver_rx_);
    update_driver_stats(now, &driver_tx_);

    if (mm_rx_stats_box_)
    {
        for (int i = mm_rx_stats_model_->rowCount() - 1, n = 0; i >= n; --i)
        {
            int time = Wt::asNumber(mm_rx_stats_model_->data(i, TIME_COLUMN, DisplayRole));
            int elapsed = time - goby_time<double>();
            if (elapsed < -mm_rx_stats_range_ - 10)
                break;
            else
                mm_rx_stats_model_->setData(i, ELAPSED_COLUMN, elapsed, DisplayRole);
        }
    }
}

void goby::common::LiaisonAcomms::update_driver_stats(int now,
                                                      LiaisonAcomms::DriverStats* driver_stats)
{
    if (driver_stats->last_time > 0)
    {
        driver_stats->last_time_text->setText(
            "Last: " + format_seconds(now - driver_stats->last_time) + " ago");

        if (now - driver_stats->last_time > 10)
            driver_stats->last_time_text->decorationStyle().setBackgroundColor(Wt::WColor());
    }
    else
    {
        driver_stats->last_time_text->setText("Never");
    }
}

void goby::common::LiaisonAcomms::dccl_analyze(const WMouseEvent& event)
{
    if (dccl_analyze_text_->isHidden())
    {
        dccl_analyze_->setText("Hide Analysis");
        dccl_analyze_text_->show();
    }
    else
    {
        dccl_analyze_->setText("Analyze");
        dccl_analyze_text_->hide();
    }
}

void goby::common::LiaisonAcomms::dccl_message(const WMouseEvent& event)
{
    if (dccl_message_text_->isHidden())
    {
        dccl_message_->setText("Hide .proto");
        dccl_message_text_->show();
    }
    else
    {
        dccl_message_->setText("Show .proto");
        dccl_message_text_->hide();
    }
}

void goby::common::LiaisonAcomms::dccl_select(WString msg)
{
    std::string m = msg.narrow();
    m = m.substr(m.find(" ") + 1);

    const google::protobuf::Descriptor* desc =
        goby::util::DynamicProtobufManager::find_descriptor(m);
    if (desc)
    {
        std::stringstream ss;
        std::lock_guard<std::mutex> l(dccl_mutex_);
        dccl_.info(desc, &ss);
        dccl_analyze_text_->setText(ss.str());

        std::string proto = desc->DebugString();
        boost::replace_all(proto, "\n", " ");
        boost::replace_all(proto, ";", ";\n");
        boost::replace_all(proto, "} ", "}\n");
        dccl_message_text_->setText(proto);
    }
    else
    {
        dccl_analyze_text_->setText("");
        dccl_message_text_->setText("");
    }
}

void goby::common::LiaisonAcomms::moos_inbox(CMOOSMsg& msg)
{
    using goby::moos::operator<<;

    if (msg.GetKey() == "ACOMMS_CONFIG" && !have_acomms_config_)
    {
        acomms_config_.ParseFromString(dccl::b64_decode(msg.GetString()));
        process_acomms_config();
        have_acomms_config_ = true;
    }
    else if (msg.GetKey() == "ACOMMS_QSIZE")
    {
        goby::acomms::protobuf::QueueSize size;
        parse_for_moos(msg.GetString(), &size);

        if (queue_bars_.count(size.dccl_id()))
        {
            queue_bars_[size.dccl_id()]->setValue(size.size());
        }
    }
    else if (msg.GetKey() == "ACOMMS_QUEUE_RECEIVE")
    {
        std::shared_ptr<google::protobuf::Message> dccl_msg =
            dynamic_parse_for_moos(msg.GetString());
        if (dccl_msg && queue_stats_.count(dccl_.id(dccl_msg->GetDescriptor())))
        {
            queue_stats_[dccl_.id(dccl_msg->GetDescriptor())].last_rx_time =
                goby::common::goby_time<double>();
        }
    }
    else if (msg.GetKey() == "ACOMMS_MAC_SLOT_START")
    {
        if (mac_slots_.count(last_slot_.slot_index()))
        {
            mac_slots_[last_slot_.slot_index()]->decorationStyle().setBackgroundColor(Wt::WColor());
            mac_slots_[last_slot_.slot_index()]->decorationStyle().setForegroundColor(Wt::WColor());
            //            mac_slots_[last_slot_.slot_index()]->disable();
            mac_bars_[last_slot_.slot_index()]->setHidden(true);
        }

        parse_for_moos(msg.GetString(), &last_slot_);

        if (mac_slots_.count(last_slot_.slot_index()))
        {
            //            mac_slots_[last_slot_.slot_index()]->enable();
            mac_bars_[last_slot_.slot_index()]->setHidden(false);
            mac_slots_[last_slot_.slot_index()]->decorationStyle().setBackgroundColor(goby_orange);
            mac_slots_[last_slot_.slot_index()]->decorationStyle().setForegroundColor(Wt::white);
        }
    }
    else if (msg.GetKey() == "ACOMMS_MODEM_TRANSMIT")
    {
        goby::acomms::protobuf::ModemTransmission tx_msg;
        parse_for_moos(msg.GetString(), &tx_msg);

        bool tx_good = true;

        for (int i = 0, n = tx_msg.ExtensionSize(micromodem::protobuf::transmit_stat); i < n; ++i)
            tx_good =
                tx_good && (tx_msg.GetExtension(micromodem::protobuf::transmit_stat, i).mode() ==
                            micromodem::protobuf::TRANSMIT_SUCCESSFUL);

        handle_modem_message(&driver_tx_, tx_good, tx_msg);
    }
    else if (msg.GetKey() == "ACOMMS_MODEM_RECEIVE")
    {
        goby::acomms::protobuf::ModemTransmission rx_msg;
        parse_for_moos(msg.GetString(), &rx_msg);

        bool rx_good = true;
        for (int i = 0, n = rx_msg.ExtensionSize(micromodem::protobuf::receive_stat); i < n; ++i)
            rx_good =
                rx_good && (rx_msg.GetExtension(micromodem::protobuf::receive_stat, i).mode() ==
                            micromodem::protobuf::RECEIVE_GOOD);

        handle_modem_message(&driver_rx_, rx_good, rx_msg);

        for (int i = 0, n = rx_msg.ExtensionSize(micromodem::protobuf::receive_stat); i < n; ++i)
        {
            const micromodem::protobuf::ReceiveStatistics& rx_stats =
                rx_msg.GetExtension(micromodem::protobuf::receive_stat, i);

            if (rx_stats.packet_type() != micromodem::protobuf::PSK)
                continue;

            std::vector<WStandardItem*> row;
            WStandardItem* time = new WStandardItem;
            time->setData(rx_stats.time() / 1e6, DisplayRole);
            row.push_back(time);

            WStandardItem* elapsed = new WStandardItem;
            elapsed->setData(0, DisplayRole);
            row.push_back(elapsed);

            WStandardItem* mse = new WStandardItem;
            mse->setData(rx_stats.mse_equalizer(), DisplayRole);
            row.push_back(mse);

            WStandardItem* snr_in = new WStandardItem;
            snr_in->setData(rx_stats.snr_in(), DisplayRole);
            row.push_back(snr_in);

            WStandardItem* snr_out = new WStandardItem;
            snr_out->setData(rx_stats.snr_out(), DisplayRole);
            row.push_back(snr_out);

            WStandardItem* doppler = new WStandardItem;
            doppler->setData(rx_stats.doppler(), DisplayRole);
            row.push_back(doppler);

            WStandardItem* bad_frames = new WStandardItem;
            bad_frames->setData(double(rx_stats.number_bad_frames()) / rx_stats.number_frames() *
                                    100.0,
                                DisplayRole);
            row.push_back(bad_frames);

            mm_rx_stats_model_->appendRow(row);
        }
    }
}

void goby::common::LiaisonAcomms::handle_modem_message(
    LiaisonAcomms::DriverStats* driver_stats, bool good,
    goby::acomms::protobuf::ModemTransmission& msg)
{
    driver_stats->last_time = msg.time() / 1e6;

    if (good)
        driver_stats->last_time_text->decorationStyle().setBackgroundColor(goby_blue);
    else
        driver_stats->last_time_text->decorationStyle().setBackgroundColor(goby_orange);

    driver_stats->last_msg_ = msg;
}

void goby::common::LiaisonAcomms::process_acomms_config()
{
    std::lock_guard<std::mutex> l(dccl_mutex_);

    // load messages in queue config
    for (int i = 0, n = acomms_config_.queue_cfg().message_entry_size(); i < n; ++i)
    {
        const goby::acomms::protobuf::QueuedMessageEntry& q =
            acomms_config_.queue_cfg().message_entry(i);

        const google::protobuf::Descriptor* desc =
            goby::util::DynamicProtobufManager::find_descriptor(q.protobuf_name());

        if (desc)
        {
            dccl_.load(desc);
            int id = dccl_.id(desc);
            queue_cfg_[id] = i;

            glog.is(DEBUG1) && glog << "Loaded: " << desc << ": " << q.protobuf_name() << std::endl;

            int row = queue_table_->rowCount();
            queue_table_->elementAt(row, 0)->addWidget(new WText(q.protobuf_name()));
            QueueBar* new_bar = new QueueBar;
            queue_table_->elementAt(row, 1)->addWidget(new_bar);

            new_bar->setMinimum(0);
            new_bar->setMaximum(q.max_queue());

            queue_bars_[id] = new_bar;

            QueueStats new_stats;
            new_stats.last_rx_time_text =
                new WText(goby::util::as<std::string>(new_stats.last_rx_time));
            queue_table_->elementAt(row, 2)->addWidget(new_stats.last_rx_time_text);
            queue_stats_[id] = new_stats;

            WPushButton* info = new WPushButton("Info");
            info->clicked().connect(boost::bind(&LiaisonAcomms::queue_info, this, _1, id));
            queue_table_->elementAt(row, 3)->addWidget(info);

            WPushButton* flush = new WPushButton("Clear");
            flush->clicked().connect(boost::bind(&LiaisonAcomms::queue_flush, this, _1, id));
            queue_table_->elementAt(row, 4)->addWidget(flush);

            if (row & 1)
                queue_table_->rowAt(row)->setStyleClass(STRIPE_ODD_CLASS);
            else
                queue_table_->rowAt(row)->setStyleClass(STRIPE_EVEN_CLASS);
        }
        else
        {
            glog.is(WARN) && glog << "Cannot find message: " << q.protobuf_name() << std::endl;
        }
    }

    // load DCCL messages into drop down box
    for (std::map<dccl::int32, const google::protobuf::Descriptor*>::const_iterator
             it = dccl_.loaded().begin(),
             it_end = dccl_.loaded().end();
         it != it_end; ++it)
    {
        dccl_combo_->addItem(
            std::string(goby::util::as<std::string>(it->first) + ": " + it->second->full_name()));
    }

    Wt::WContainerWidget* mac_top_box = new Wt::WContainerWidget(amac_box_);

    new Wt::WText("Number of slots in the cycle: " +
                      goby::util::as<std::string>(acomms_config_.mac_cfg().slot_size()) + ".",
                  mac_top_box);
    mac_cycle_bar_ = new MACBar(mac_top_box);
    mac_cycle_bar_->setMinimum(0);
    int cycle_length = 0;
    for (int i = 0, n = acomms_config_.mac_cfg().slot_size(); i < n; ++i)
        cycle_length += acomms_config_.mac_cfg().slot(i).slot_seconds();

    mac_cycle_bar_->setMaximum(cycle_length);
    mac_cycle_bar_->setFloatSide(Wt::Right);

    new Wt::WText("<hr/>", mac_top_box);

    mac_slot_style_.setBorder(Wt::WBorder(Wt::WBorder::Solid, Wt::WBorder::Thin));
    for (int i = 0, n = acomms_config_.mac_cfg().slot_size(); i < n; ++i)
    {
        const goby::acomms::protobuf::ModemTransmission& slot = acomms_config_.mac_cfg().slot(i);

        Wt::WContainerWidget* box = new Wt::WContainerWidget(amac_box_);
        box->setDecorationStyle(mac_slot_style_);
        box->clicked().connect(boost::bind(&LiaisonAcomms::mac_info, this, _1, i));

        double height = std::log10(slot.slot_seconds());
        box->setPadding(Wt::WLength(height / 2, Wt::WLength::FontEm), Wt::Top | Wt::Bottom);
        box->setPadding(Wt::WLength(3), Wt::Right | Wt::Left);
        box->setMargin(Wt::WLength(3));
        box->resize(Wt::WLength(), Wt::WLength(1 + height, Wt::WLength::FontEm));

        std::string slot_text;
        if (slot.type() == goby::acomms::protobuf::ModemTransmission::UNKNOWN)
        {
            slot_text = "Non-Acomms Slot";
            if (slot.has_unique_id())
                slot_text += " (" + goby::util::as<std::string>(slot.unique_id()) + ")";
        }
        else if (slot.type() == goby::acomms::protobuf::ModemTransmission::DATA)
        {
            std::stringstream ss;
            ss << "Data Slot | Source: " << slot.src();
            if (slot.dest() != -1)
                ss << " | Dest: " << slot.dest();
            ss << " | Rate: " << slot.rate();
            slot_text = ss.str();
        }
        else
        {
            slot_text = slot.DebugString();
        }

        new Wt::WText(slot_text, box);

        //        new Wt::WBreak(box);

        MACBar* new_bar = new MACBar(box);
        new_bar->setMinimum(0);
        new_bar->setMaximum(slot.slot_seconds());
        new_bar->setFloatSide(Wt::Right);
        new_bar->setHidden(true);

        mac_bars_[i] = new_bar;

        //        box->disable();

        mac_slots_[i] = box;
    }

    driver_panel_->setTitle("Driver: " +
                            goby::acomms::protobuf::DriverType_Name(acomms_config_.driver_type()));

    if (acomms_config_.driver_type() == goby::acomms::protobuf::DRIVER_WHOI_MICROMODEM)
    {
        mm_rx_stats_box_ = new WGroupBox("WHOI Micro-Modem Receive Statistics", driver_box_);
        mm_rx_stats_model_ = new WStandardItemModel(0, MAX_COLUMN + 1, mm_rx_stats_box_);

        mm_rx_stats_graph_ = new Chart::WCartesianChart(Chart::ScatterPlot, mm_rx_stats_box_);
        mm_rx_stats_graph_->setModel(mm_rx_stats_model_);
        mm_rx_stats_graph_->setXSeriesColumn(ELAPSED_COLUMN);

        for (int i = MSE_COLUMN, n = MAX_COLUMN; i <= n; ++i)
        {
            Chart::WDataSeries s(i, Chart::LineSeries);
            Chart::MarkerType type;
            switch (i)
            {
                default:
                case MSE_COLUMN: type = Chart::CircleMarker; break;
                case SNR_IN_COLUMN: type = Chart::SquareMarker; break;
                case SNR_OUT_COLUMN: type = Chart::CrossMarker; break;
                case DOPPLER_COLUMN: type = Chart::TriangleMarker; break;
                case PERCENT_BAD_FRAMES_COLUMN: type = Chart::XCrossMarker; break;
            }
            s.setMarker(type);
            s.setHidden(true);
            mm_rx_stats_graph_->addSeries(s);
        }

        mm_rx_stats_graph_->axis(Chart::XAxis).setTitle("Seconds ago");

        mm_rx_stats_model_->setHeaderData(MSE_COLUMN, std::string("MSE"));
        mm_rx_stats_model_->setHeaderData(SNR_IN_COLUMN, std::string("SNR In"));
        mm_rx_stats_model_->setHeaderData(SNR_OUT_COLUMN, std::string("SNR Out"));
        mm_rx_stats_model_->setHeaderData(DOPPLER_COLUMN, std::string("Doppler"));
        mm_rx_stats_model_->setHeaderData(PERCENT_BAD_FRAMES_COLUMN, std::string("% bad frames"));

        mm_rx_stats_graph_->setPlotAreaPadding(120, Right);
        mm_rx_stats_graph_->setPlotAreaPadding(50, Left);
        mm_rx_stats_graph_->setPlotAreaPadding(40, Top | Bottom);
        mm_rx_stats_graph_->setMargin(10, Top | Bottom);            // add margin vertically
        mm_rx_stats_graph_->setMargin(WLength::Auto, Left | Right); // center horizontally
        mm_rx_stats_graph_->resize(Wt::WLength(40, Wt::WLength::FontEm), 300);

        mm_rx_stats_graph_->setLegendEnabled(true);

        mm_range(mm_rx_stats_range_);

        mm_rx_stats_graph_->axis(Chart::Y1Axis).setVisible(false);
        mm_rx_stats_graph_->axis(Chart::Y1Axis).setVisible(false);

        WGroupBox* y1axis_group = new Wt::WGroupBox("Y1 Axis", mm_rx_stats_box_);
        y1axis_group->resize(Wt::WLength(45, Wt::WLength::Percentage), Wt::WLength::Auto);
        y1axis_group->setInline(true);
        WGroupBox* y2axis_group = new Wt::WGroupBox("Y2 Axis", mm_rx_stats_box_);
        y2axis_group->resize(Wt::WLength(45, Wt::WLength::Percentage), Wt::WLength::Auto);
        y2axis_group->setInline(true);
        for (int i = MSE_COLUMN, n = MAX_COLUMN; i <= n; ++i)
        {
            for (int j = 0, m = 2; j < m; ++j)
            {
                mm_rx_chks_[i][j] = new WCheckBox(asString(mm_rx_stats_model_->headerData(i)),
                                                  j == 0 ? y1axis_group : y2axis_group);
                mm_rx_chks_[i][j]->resize(Wt::WLength(40, Wt::WLength::Percentage),
                                          Wt::WLength::Auto);

                mm_rx_chks_[i][j]->checked().connect(
                    boost::bind(&LiaisonAcomms::mm_check, this, j, i, true));
                mm_rx_chks_[i][j]->unChecked().connect(
                    boost::bind(&LiaisonAcomms::mm_check, this, j, i, false));
            }
        }

        mm_rx_chks_[MSE_COLUMN][0]->setChecked(true);
        mm_check(0, MSE_COLUMN, true);

        new Wt::WBreak(mm_rx_stats_box_);

        new Wt::WText("X Axis Range (seconds):", mm_rx_stats_box_);

        WDoubleSpinBox* range_box = new WDoubleSpinBox(mm_rx_stats_box_);
        range_box->setMinimum(10);
        range_box->setMaximum(1e100);
        range_box->setSingleStep(600);
        range_box->setDecimals(0);
        range_box->setValue(mm_rx_stats_range_);
        range_box->valueChanged().connect(this, &LiaisonAcomms::mm_range);
    }
}

void goby::common::LiaisonAcomms::mm_check(int axis, int column, bool checked)
{
    std::cout << "Axis: " << axis << ", column: " << column << ", check: " << checked << std::endl;

    Chart::WDataSeries& s = mm_rx_stats_graph_->series(column);
    s.setHidden(!checked);
    s.bindToAxis(axis == 0 ? Chart::Y1Axis : Chart::Y2Axis);

    // uncheck the corresponding other axis
    if (checked)
        mm_rx_chks_[column][axis == 0 ? 1 : 0]->setChecked(false);

    bool axis_enabled[2] = {false, false};
    for (std::map<int, std::map<int, Wt::WCheckBox*> >::iterator it = mm_rx_chks_.begin(),
                                                                 end = mm_rx_chks_.end();
         it != end; ++it)
    {
        for (int i = 0, n = 2; i < n; ++i)
        {
            if (it->second[i]->isChecked())
                axis_enabled[i] = true;
        }
    }

    for (int i = 0, n = 2; i < n; ++i)
        mm_rx_stats_graph_->axis(i == 0 ? Chart::Y1Axis : Chart::Y2Axis)
            .setVisible(axis_enabled[i]);
}

void goby::common::LiaisonAcomms::mm_range(double range)
{
    mm_rx_stats_range_ = range;
    mm_rx_stats_graph_->axis(Chart::XAxis).setRange(-mm_rx_stats_range_, 0);
    mm_rx_stats_graph_->axis(Chart::XAxis).setLabelInterval(mm_rx_stats_range_ / 5);
}

Wt::WString goby::common::QueueBar::text() const
{
    return std::string(goby::util::as<std::string>(value()) + "/" +
                       goby::util::as<std::string>(maximum()));
}

Wt::WString goby::common::MACBar::text() const
{
    return std::string(goby::util::as<std::string>(value()) + "/" +
                       goby::util::as<std::string>(maximum()) + " s");
}

void goby::common::LiaisonAcomms::queue_info(const Wt::WMouseEvent& event, int id)
{
    const google::protobuf::Descriptor* desc = 0;
    {
        std::lock_guard<std::mutex> l(dccl_mutex_);
        if (dccl_.loaded().count(id))
            desc = dccl_.loaded().find(id)->second;
    }

    if (!desc)
        return;

    glog.is(DEBUG1) && glog << "Queue info for " << desc->full_name() << std::endl;

    WDialog dialog("Queue info for " + desc->full_name());
    WContainerWidget* message_div = new WContainerWidget(dialog.contents());
    new WText(std::string("<pre>" +
                          acomms_config_.queue_cfg().message_entry(queue_cfg_[id]).DebugString() +
                          "</pre>"),
              message_div);

    message_div->setOverflow(WContainerWidget::OverflowAuto);

    WPushButton ok("OK", dialog.contents());

    dialog.rejectWhenEscapePressed();
    ok.clicked().connect(&dialog, &WDialog::accept);

    if (dialog.exec() == WDialog::Accepted) {}
}

void goby::common::LiaisonAcomms::mac_info(const Wt::WMouseEvent& event, int id)
{
    WDialog dialog("AMAC Slot Info for Slot #" + goby::util::as<std::string>(id));
    WContainerWidget* message_div = new WContainerWidget(dialog.contents());
    new WText(std::string("<pre>" + acomms_config_.mac_cfg().slot(id).DebugString() + "</pre>"),
              message_div);

    message_div->setOverflow(WContainerWidget::OverflowAuto);

    WPushButton ok("OK", dialog.contents());

    dialog.rejectWhenEscapePressed();
    ok.clicked().connect(&dialog, &WDialog::accept);

    if (dialog.exec() == WDialog::Accepted) {}
}

void goby::common::LiaisonAcomms::driver_info(const Wt::WMouseEvent& event,
                                              LiaisonAcomms::DriverStats* driver_stats)
{
    WDialog dialog("Last Message " +
                   std::string(driver_stats->direction == RX ? "Received" : "Transmitted"));
    WContainerWidget* message_div = new WContainerWidget(dialog.contents());
    WText* txt = new WText(std::string(driver_stats->last_msg_.DebugString()), message_div);
    txt->setTextFormat(PlainText);

    message_div->setMaximumSize(800, 600);
    message_div->setOverflow(WContainerWidget::OverflowAuto);

    WPushButton ok("OK", dialog.contents());

    dialog.rejectWhenEscapePressed();
    ok.clicked().connect(&dialog, &WDialog::accept);

    if (dialog.exec() == WDialog::Accepted) {}
}

void goby::common::LiaisonAcomms::queue_flush(const Wt::WMouseEvent& event, int id)
{
    goby::acomms::protobuf::QueueFlush flush;
    flush.set_dccl_id(id);
    std::string serialized;
    serialize_for_moos(&serialized, flush);

    MOOSNode::send(CMOOSMsg(MOOS_NOTIFY, "ACOMMS_FLUSH_QUEUE", serialized),
                   LIAISON_INTERNAL_PUBLISH_SOCKET);
}

std::string goby::common::LiaisonAcomms::format_seconds(int sec)
{
    if (sec < 60)
        return goby::util::as<std::string>(sec) + " s";
    else
        return goby::util::as<std::string>(sec / 60) + ":" + ((sec % 60 < 10) ? "0" : "") +
               goby::util::as<std::string>(sec % 60);
}
