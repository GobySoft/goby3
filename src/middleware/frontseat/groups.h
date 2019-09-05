#include "goby/middleware/group.h"

#ifndef GROUPS_20190816H
#define GROUPS_20190816H

namespace goby
{
namespace middleware
{
namespace frontseat
{
namespace groups
{
constexpr goby::middleware::Group node_status{"goby::middleware::frontseat::node_status"};
constexpr goby::middleware::Group desired_course{"goby::middleware::frontseat::desired_course"};

} // namespace groups
} // namespace frontseat
} // namespace middleware
} // namespace goby

#endif
