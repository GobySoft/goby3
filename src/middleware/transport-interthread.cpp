#include "transport-interthread.h"

std::unordered_map<std::type_index, std::unique_ptr<goby::SubscriptionStoreBase>> goby::SubscriptionStoreBase::stores_;
std::shared_timed_mutex goby::SubscriptionStoreBase::stores_mutex_;
