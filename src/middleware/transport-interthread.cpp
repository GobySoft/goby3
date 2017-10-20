#include "transport-interthread.h"

std::unordered_map<std::type_index, std::unique_ptr<goby::SubscriptionStoreBase>> goby::SubscriptionStoreBase::stores_;
std::timed_mutex goby::SubscriptionStoreBase::stores_mutex_;
std::condition_variable_any goby::SubscriptionStoreBase::stores_cv_;
std::atomic<int> goby::SubscriptionStoreBase::pollers_(0);