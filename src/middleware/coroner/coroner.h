// Copyright 2017-2022:
//   GobySoft, LLC (2013-)
//   Community contributors (see AUTHORS file)
// File authors:
//   Toby Schneider <toby@gobysoft.org>
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

#ifndef GOBY_MIDDLEWARE_CORONER_FUNCTIONS_H
#define GOBY_MIDDLEWARE_CORONER_FUNCTIONS_H

#include "goby/middleware/marshalling/protobuf.h"

#include "goby/middleware/coroner/groups.h"
#include "goby/middleware/protobuf/coroner.pb.h"
#include "goby/middleware/transport/interthread.h"

namespace goby
{
namespace middleware
{

namespace coroner
{

template <typename Derived> class Thread
{
  protected:
    void subscribe_coroner()
    {
        static_cast<Derived*>(this)->interthread().template subscribe<groups::health_request>(
            [this](const protobuf::HealthRequest& request)
            {
                std::shared_ptr<protobuf::ThreadHealth> response(new protobuf::ThreadHealth);
                static_cast<Derived*>(this)->thread_health(*response);
                static_cast<Derived*>(this)
                    ->interthread()
                    .template publish<groups::health_response>(response);
            });
    }
};

template <typename Derived> class Application
{
  protected:
    void subscribe_coroner()
    {
        static_cast<Derived*>(this)->interprocess().template subscribe<groups::health_request>(
            [this](const protobuf::HealthRequest& request)
            {
                auto health_response = std::make_shared<protobuf::ProcessHealth>();

                health_response->set_name(static_cast<Derived*>(this)->app_name());
                health_response->set_pid(getpid());

                static_cast<Derived*>(this)->thread_health(*health_response->mutable_main());
                static_cast<Derived*>(this)
                    ->interprocess()
                    .template publish<groups::health_response>(health_response);
            });
    }
};

template <typename Derived> class ApplicationInterThread
{
  protected:
    void subscribe_coroner()
    {
        static_cast<Derived*>(this)->interthread().template subscribe<groups::health_request>(
            [this](const protobuf::HealthRequest& request)
            {
                auto health_response = std::make_shared<protobuf::ProcessHealth>();

                health_response->set_name(static_cast<Derived*>(this)->app_name());
                health_response->set_pid(getpid());

                preseed_hook(health_response);

                static_cast<Derived*>(this)->thread_health(*health_response->mutable_main());
                static_cast<Derived*>(this)
                    ->interthread()
                    .template publish<groups::health_response>(health_response);
            });
    }
    virtual void preseed_hook(std::shared_ptr<protobuf::ProcessHealth>& ph) {}
};

} // namespace coroner
} // namespace middleware
} // namespace goby

#endif
