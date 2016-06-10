#ifndef TransportIntraProcess20160609H
#define TransportIntraProcess20160609H

#include <thread>
#include <typeindex>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <set>

#include "transport-common.h"

namespace goby
{
    typedef std::string Group;
    
    extern std::recursive_timed_mutex subscription_mutex;
    class SubscriptionStoreBase
    {
    public:
        // returns number of data items posted to callbacks 
        static int poll_all(std::thread::id thread_id, const std::chrono::system_clock::time_point& timeout_time)
        {
            if(stores_mutex_.try_lock_until(timeout_time))
            {
                int poll_items = 0;
                for (auto const &s : stores_)
                    poll_items += s.second->poll(thread_id, timeout_time);
                stores_mutex_.unlock();
                return poll_items;
            }
            else
            {
                return 0;
            }
        }

    protected:
        template<typename StoreType> 
            static void insert()
        {
            // check the store, and if there isn't one for this type, create one
            std::lock_guard<decltype(stores_mutex_)> lock(stores_mutex_);   
            auto index = std::type_index(typeid(StoreType));
            if(!stores_.count(index))
                stores_.insert(std::make_pair(index, std::unique_ptr<StoreType>(new StoreType)));
        }
        
    protected:
        virtual int poll(std::thread::id thread_id, const std::chrono::system_clock::time_point& timeout_time) = 0;
        

    private:
        // stores a map of DataTypes to SubscriptionStores so that can call poll() on all the stores
        static std::unordered_map<std::type_index, std::unique_ptr<SubscriptionStoreBase>> stores_;
        static std::timed_mutex stores_mutex_;
    };

    
    template<typename DataType>
        class SubscriptionStore : public SubscriptionStoreBase
    {
    public:

        static void subscribe(const Group& group, std::function<void(std::shared_ptr<const DataType>)> func, std::thread::id thread_id, std::shared_ptr<std::condition_variable_any> cv)
        {
            std::lock_guard<decltype(subscription_mutex)> lock(subscription_mutex);
            // insert callback
            auto it = subscription_callbacks_.insert(std::make_pair(thread_id, Callback(group, func)));
            // insert group with interator to callback
            subscription_groups_.insert(std::make_pair(group, it));

            // if we don't have a condition variable already for this thread, store it
            if(!data_condition_.count(thread_id))
                data_condition_.insert(std::make_pair(thread_id, cv));

            // try inserting a copy of this templated class via the base class for SubscriptionStoreBase::poll_all to use
            SubscriptionStoreBase::insert<SubscriptionStore<DataType>>();
        }

        static void publish(std::shared_ptr<const DataType> data, const Group& group)
        {
            // push new data
            // build up local vector of relevant condition variables while locked
            std::set<std::shared_ptr<std::condition_variable_any>> cv_to_notify;
            {
                std::lock_guard<decltype(subscription_mutex)> lock(subscription_mutex);
                auto range = subscription_groups_.equal_range(group);
                for (auto it = range.first; it != range.second; ++it)
                {
                    std::thread::id thread_id = it->second->first;
                    auto queue_it = data_.find(thread_id);
                    if(queue_it == data_.end())
                        queue_it = data_.insert(std::make_pair(thread_id, DataQueue())).first;
                    queue_it->second.insert(group, data);
                    cv_to_notify.insert(data_condition_.at(thread_id));
                }
            }

            // unlock and notify condition variables from local vector
            for (auto cv : cv_to_notify)
                cv->notify_all();
        }

    private:
        int poll(std::thread::id thread_id, const std::chrono::system_clock::time_point& timeout_time)
        {
            std::lock_guard<decltype(subscription_mutex)> lock(subscription_mutex);

            auto queue_it = data_.find(thread_id);
            if(queue_it == data_.end())
                queue_it = data_.insert(std::make_pair(thread_id, DataQueue())).first;

            if(queue_it->second.empty())
                return 0;

            int poll_items_count = 0;
            // loop over all Groups stored in this DataQueue
            for (auto data_it = queue_it->second.cbegin(), end = queue_it->second.cend(); data_it != end; ++data_it) 
            {
                const Group& group = data_it->first;
                auto group_range = subscription_groups_.equal_range(group);
                // For a given Group, loop over all subscriptions to this Group
                for(auto group_it = group_range.first; group_it != group_range.second; ++group_it)
                {
                    if(group_it->second->first != thread_id)
                        continue;
                    
                    auto& callback = group_it->second->second.callback;
                    // actually call the callback function for all the elements queued
                    for(auto datum : data_it->second)
                    {
                        ++poll_items_count;
                        callback(datum);
                    }
                }
            }

            queue_it->second.clear();
            return poll_items_count;
        }
            
    private:
        struct Callback
        {
        Callback(const Group& g, const std::function<void(std::shared_ptr<const DataType>)>& c) : group(g), callback(c) {}
            Group group;
            std::function<void(std::shared_ptr<const DataType>)> callback;
        };

        class DataQueue
        {
        private:
            std::map<Group, std::vector<std::shared_ptr<const DataType>>> data_;
        public:
            void insert(const Group& g, std::shared_ptr<const DataType> datum)
            { data_[g].push_back(datum); }
            void clear()
            { data_.clear(); }
            bool empty()
            { return data_.empty(); }
            typename decltype(data_)::const_iterator cbegin()
            { return data_.begin(); }
            typename decltype(data_)::const_iterator cend()
            { return data_.end(); }
        };
        
        
        
        // subscriptions for a given thread
        static std::unordered_multimap<std::thread::id, Callback> subscription_callbacks_;
        // threads that are subscribed to a given group
        static std::unordered_multimap<Group, typename decltype(subscription_callbacks_)::const_iterator> subscription_groups_;
        // condition variable to use for data
        static std::unordered_map<std::thread::id, std::shared_ptr<std::condition_variable_any>> data_condition_;
        
        // data for a given thread
        static std::unordered_map<std::thread::id, DataQueue> data_;
        
        
    };        
        
    template<typename DataType>
        std::unordered_multimap<std::thread::id, typename SubscriptionStore<DataType>::Callback> SubscriptionStore<DataType>::subscription_callbacks_;
    template<typename DataType>
        std::unordered_map<std::thread::id, typename SubscriptionStore<DataType>::DataQueue> SubscriptionStore<DataType>::data_;            
    template<typename DataType>
        std::unordered_multimap<Group, typename decltype(SubscriptionStore<DataType>::subscription_callbacks_)::const_iterator> SubscriptionStore<DataType>::subscription_groups_;
    template<typename DataType>
        std::unordered_map<std::thread::id, std::shared_ptr<std::condition_variable_any>> SubscriptionStore<DataType>::data_condition_;


    class IntraProcessTransporter
    {
    public:
    IntraProcessTransporter() :
        cv_(std::make_shared<std::condition_variable_any>())
        { }
        
        
        template<typename DataType, int scheme = scheme<DataType>()>
            void publish(const DataType& data, const std::string& group, const TransporterConfig& transport_cfg = TransporterConfig())
        {
            std::cout << "IntraProcessTransporter const ref publish" << std::endl; 
        }

        template<typename DataType, int scheme = scheme<DataType>()>
            void publish(std::shared_ptr<DataType> data, const std::string& group, const TransporterConfig& transport_cfg = TransporterConfig())
        {
            SubscriptionStore<DataType>::publish(data, group);
        }

        
        template<typename DataType, int scheme = scheme<DataType>(), class Function>
            void subscribe(const std::string& group, Function f, std::thread::id thread_id)
        {
            std::function<void(std::shared_ptr<const DataType>)> func(f);
            SubscriptionStore<DataType>::subscribe(group, func, thread_id, cv_);
        }
        
        template<typename DataType, int scheme = scheme<DataType>(), class C>
            void subscribe(const std::string& group, void(C::*mem_func)(std::shared_ptr<const DataType>), C* c, std::thread::id thread_id)
        {
            subscribe<DataType, scheme>(group, std::bind(mem_func, c, std::placeholders::_1), thread_id);
        }

        int poll(std::thread::id thread_id, const std::chrono::system_clock::time_point& timeout = std::chrono::system_clock::time_point::max())
        {
            std::unique_lock<decltype(subscription_mutex)> lock(subscription_mutex);
            int poll_items = SubscriptionStoreBase::poll_all(thread_id, timeout);

            while(poll_items == 0) // no items, so wait
            {
                //                std::cout << thread_id << ": waiting on condition var" << std::endl;
                if(cv_->wait_until(lock, timeout) == std::cv_status::no_timeout)
                {
                    poll_items = SubscriptionStoreBase::poll_all(thread_id, timeout);
                }
                else
                {
                    return poll_items;
                }
            }

            return poll_items;
        }
        
        int poll(std::thread::id thread_id, std::chrono::system_clock::duration wait_for)
        {
            poll(thread_id, std::chrono::system_clock::now() + wait_for);
        }
        
    private:
        std::shared_ptr<std::condition_variable_any> cv_;
    };
    
}

#endif
