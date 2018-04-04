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

#ifndef TransportInterfaces20170808H
#define TransportInterfaces20170808H

#include <memory>
#include <mutex>
#include <condition_variable>

#include "serialize_parse.h"
#include "group.h"

#include "goby/middleware/protobuf/transporter_config.pb.h"
#include "goby/common/logger.h"

namespace goby
{
    class NullTransporter;
    
    template<typename Transporter, typename InnerTransporter>
        class StaticTransporterInterface
    {
    public:
        template<const Group& group, typename Data, int scheme = transporter_scheme<Data, Transporter>()>
            void publish(const Data& data, const goby::protobuf::TransporterConfig& transport_cfg = goby::protobuf::TransporterConfig())
            {
                check_validity<group>();
                static_cast<Transporter*>(this)->template publish_dynamic<Data, scheme>(data, group, transport_cfg);
            }

        // need both const and non-const shared_ptr overload to ensure that the const& overload isn't preferred to these.
        template<const Group& group, typename Data, int scheme = transporter_scheme<Data, Transporter>()>
            void publish(std::shared_ptr<const Data> data, const goby::protobuf::TransporterConfig& transport_cfg = goby::protobuf::TransporterConfig())
            {
                check_validity<group>();
                static_cast<Transporter*>(this)->template publish_dynamic<Data, scheme>(data, group, transport_cfg);
            }
        
        template<const Group& group, typename Data, int scheme = transporter_scheme<Data, Transporter>()>
            void publish(std::shared_ptr<Data> data, const goby::protobuf::TransporterConfig& transport_cfg = goby::protobuf::TransporterConfig())
            {
                publish<group, Data, scheme>(std::shared_ptr<const Data>(data), transport_cfg);
            }

        template<const Group& group, typename Data, int scheme = transporter_scheme<Data, Transporter>()>
            void subscribe(std::function<void(const Data&)> f)
            {
                check_validity<group>();
                static_cast<Transporter*>(this)->template subscribe_dynamic<Data, scheme>(f, group);
            }
        template<const Group& group, typename Data, int scheme = transporter_scheme<Data, Transporter>()>
            void subscribe(std::function<void(std::shared_ptr<const Data>)> f)
            {
                check_validity<group>();
                static_cast<Transporter*>(this)->template subscribe_dynamic<Data, scheme>(f, group);
            }

        template<const Group& group, typename Data, int scheme = transporter_scheme<Data, Transporter>()>
            void unsubscribe()
            {
                check_validity<group>();
                static_cast<Transporter*>(this)->template unsubscribe_dynamic<Data, scheme>(group);
            }
        
        InnerTransporter& inner()
        {
            static_assert(!std::is_same<InnerTransporter, NullTransporter>(), "This transporter has no inner() transporter layer");
            return static_cast<Transporter*>(this)->inner_;
        }
    };


    class PollerInterface
    {
    public:
    PollerInterface(std::shared_ptr<std::timed_mutex> poll_mutex,
                    std::shared_ptr<std::condition_variable_any> cv) :
        poll_mutex_(poll_mutex),
            cv_(cv)
            { }
        
        
        int poll(const std::chrono::system_clock::time_point& timeout = std::chrono::system_clock::time_point::max());
        int poll(std::chrono::system_clock::duration wait_for);

        std::shared_ptr<std::timed_mutex> poll_mutex() { return poll_mutex_; }
        std::shared_ptr<std::condition_variable_any> cv() { return cv_; }
        
    private:
        template<typename Transporter> friend class Poller;
        // poll the transporter for data
        virtual int _transporter_poll(std::unique_ptr<std::unique_lock<std::timed_mutex>>& lock) = 0;

    private:
        // poll all the transporters for data, including a timeout (only called by the outside-most Poller)
        int _poll_all(const std::chrono::system_clock::time_point& timeout);
        
        std::shared_ptr<std::timed_mutex> poll_mutex_;
        // signaled when there's no data for this thread to read during _poll()
        std::shared_ptr<std::condition_variable_any> cv_;
    };
}


#endif
