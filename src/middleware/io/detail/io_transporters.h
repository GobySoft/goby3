// Copyright 2019-2020:
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

#ifndef GOBY_MIDDLEWARE_IO_DETAIL_IO_TRANSPORTERS_H
#define GOBY_MIDDLEWARE_IO_DETAIL_IO_TRANSPORTERS_H

#include "goby/exception.h"

namespace goby
{
namespace middleware
{
class Group;
class InterThreadTransporter;
template <typename InnerTransporter> class InterProcessForwarder;
namespace io
{
enum class PubSubLayer
{
    INTERTHREAD,
    INTERPROCESS
};

namespace detail
{
enum class Direction
{
    PUBLISH,
    SUBSCRIBE
};

// Direction template parameter required to avoid diamond inheritance problem with IOThread
template <class Derived, Direction direction, PubSubLayer layer> struct IOTransporterByLayer
{
};

template <class Derived, Direction direction>
struct IOTransporterByLayer<Derived, direction, PubSubLayer::INTERTHREAD>
{
  protected:
    using Transporter = InterThreadTransporter;
    Transporter& io_transporter() { return static_cast<Derived*>(this)->interthread(); }
};

template <class Derived, Direction direction>
struct IOTransporterByLayer<Derived, direction, PubSubLayer::INTERPROCESS>
{
  protected:
    using Transporter = InterProcessForwarder<InterThreadTransporter>;
    Transporter& io_transporter() { return static_cast<Derived*>(this)->interprocess(); }
};

template <class Derived, const goby::middleware::Group& line_in_group, PubSubLayer layer,
          bool use_indexed_group>
struct IOPublishTransporter
{
};

template <class Derived, const goby::middleware::Group& line_in_group, PubSubLayer layer>
struct IOPublishTransporter<Derived, line_in_group, layer, false>
    : IOTransporterByLayer<Derived, Direction::PUBLISH, layer>
{
    IOPublishTransporter(int index) {}
    template <
        typename Data,
        int scheme = transporter_scheme<
            Data, typename IOTransporterByLayer<Derived, Direction::PUBLISH, layer>::Transporter>()>
    void publish_in(std::shared_ptr<Data> data)
    {
        this->io_transporter().template publish<line_in_group, Data, scheme>(data);
    }
};

template <class Derived, const goby::middleware::Group& line_in_group, PubSubLayer layer>
struct IOPublishTransporter<Derived, line_in_group, layer, true>
    : IOTransporterByLayer<Derived, Direction::PUBLISH, layer>

{
    IOPublishTransporter(int index)
        : in_group_(std::string(line_in_group), index == -1 ? Group::invalid_numeric_group : index)
    {
        if (index > Group::maximum_valid_group)
            throw(goby::Exception("Index must be less than or equal to: " +
                                  std::to_string(Group::maximum_valid_group)));
    }
    template <
        typename Data,
        int scheme = transporter_scheme<
            Data, typename IOTransporterByLayer<Derived, Direction::PUBLISH, layer>::Transporter>()>
    void publish_in(std::shared_ptr<Data> data)
    {
        this->io_transporter().template publish_dynamic<Data, scheme>(data, in_group_);
    }

  private:
    DynamicGroup in_group_;
};

template <class Derived, const goby::middleware::Group& line_out_group, PubSubLayer layer,
          bool use_indexed_group>
struct IOSubscribeTransporter
{
};

template <class Derived, const goby::middleware::Group& line_out_group, PubSubLayer layer>
struct IOSubscribeTransporter<Derived, line_out_group, layer, false>
    : IOTransporterByLayer<Derived, Direction::SUBSCRIBE, layer>
{
    IOSubscribeTransporter(int index) {}

    template <typename Data,
              int scheme = transporter_scheme<
                  Data, typename IOTransporterByLayer<Derived, Direction::SUBSCRIBE,
                                                      layer>::Transporter>(),
              Necessity necessity = Necessity::OPTIONAL>
    void subscribe_out(std::function<void(std::shared_ptr<const Data>)> f)
    {
        this->io_transporter().template subscribe<line_out_group, Data, scheme, necessity>(f);
    }

    template <typename Data, int scheme = transporter_scheme<
                                 Data, typename IOTransporterByLayer<Derived, Direction::SUBSCRIBE,
                                                                     layer>::Transporter>()>
    void unsubscribe_out()
    {
        this->io_transporter().template unsubscribe<line_out_group, Data, scheme>();
    }
};

template <class Derived, const goby::middleware::Group& line_out_group, PubSubLayer layer>
struct IOSubscribeTransporter<Derived, line_out_group, layer, true>
    : IOTransporterByLayer<Derived, Direction::SUBSCRIBE, layer>
{
    IOSubscribeTransporter(int index)
        : out_group_(std::string(line_out_group),
                     index == -1 ? Group::invalid_numeric_group : index)
    {
        if (index > Group::maximum_valid_group)
            throw(goby::Exception("Index must be less than or equal to: " +
                                  std::to_string(Group::maximum_valid_group)));
    }

    template <typename Data,
              int scheme = transporter_scheme<
                  Data, typename IOTransporterByLayer<Derived, Direction::SUBSCRIBE,
                                                      layer>::Transporter>(),
              Necessity necessity = Necessity::OPTIONAL>
    void subscribe_out(std::function<void(std::shared_ptr<const Data>)> f)
    {
        this->io_transporter().template subscribe_dynamic<Data, scheme>(f, out_group_);
    }

    template <typename Data, int scheme = transporter_scheme<
                                 Data, typename IOTransporterByLayer<Derived, Direction::SUBSCRIBE,
                                                                     layer>::Transporter>()>
    void unsubscribe_out()
    {
        this->io_transporter().template unsubscribe_dynamic<Data, scheme>(out_group_);
    }

  private:
    DynamicGroup out_group_;
};

} // namespace detail
} // namespace io
} // namespace middleware
} // namespace goby

#endif
