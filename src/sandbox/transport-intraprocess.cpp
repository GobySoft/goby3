#include "transport-intraprocess.h"

std::unordered_map<std::type_index, std::unique_ptr<goby::SubscriptionStoreBase>> goby::SubscriptionStoreBase::stores_;
std::timed_mutex goby::SubscriptionStoreBase::stores_mutex_;
std::recursive_timed_mutex goby::subscription_mutex;
