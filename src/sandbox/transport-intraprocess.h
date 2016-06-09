#ifndef TransportIntraProcess20160609H
#define TransportIntraProcess20160609H

#include <thread>
#include <typeindex>
#include <memory>
#include <mutex>
#include <condition_variable>

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
            std::lock_guard<decltype(stores_mutex_)> lock(stores_mutex_);   
            auto index = std::type_index(typeid(StoreType));
            if(!stores_.count(index))
                stores_.insert(std::make_pair(index, std::unique_ptr<StoreType>(new StoreType)));
        }
        
    protected:
        virtual int poll(std::thread::id thread_id, const std::chrono::system_clock::time_point& timeout_time) = 0;
        

    private:
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
            auto it = subscription_callbacks_.insert(std::make_pair(thread_id, std::make_pair(group, func)));
            subscription_groups_.insert(std::make_pair(group, it));

            if(!data_condition_.count(thread_id))
                data_condition_.insert(std::make_pair(thread_id, cv));
            
            SubscriptionStoreBase::insert<SubscriptionStore<DataType>>();
        }

        static void publish(std::shared_ptr<const DataType> data, const Group& group)
        {
            {
                std::lock_guard<decltype(subscription_mutex)> lock(subscription_mutex);
                auto range = subscription_groups_.equal_range(group);
                for (auto it = range.first; it != range.second; ++it)
                {
                    std::thread::id thread_id = it->second->first;
                    data_.insert(std::make_pair(thread_id, std::make_pair(group, data)));
                }
            }

            auto range = subscription_groups_.equal_range(group);
            for (auto it = range.first; it != range.second; ++it)
            {
                std::thread::id thread_id = it->second->first;
                data_condition_.at(thread_id)->notify_all();
            }
        }

    private:
        
        int poll(std::thread::id thread_id, const std::chrono::system_clock::time_point& timeout_time)
        {
            std::lock_guard<decltype(subscription_mutex)> lock(subscription_mutex);
            
            auto poll_items_count = data_.count(thread_id);
            if(poll_items_count == 0)
                return poll_items_count;
            
            auto data_range = data_.equal_range(thread_id);
            for (auto data_it = data_range.first; data_it != data_range.second; ++data_it) 
            {
                const Group& group = data_it->second.first;
                auto group_range = subscription_groups_.equal_range(group);
                for(auto group_it = group_range.first; group_it != group_range.second; ++group_it)
                {
                    auto& callback = group_it->second->second.second;
                    callback(data_it->second.second);
                }
            }

            data_.erase(data_range.first, data_range.second);
            return poll_items_count;
        }
            
    private:

        // subscriptions for a given thread
        static std::unordered_multimap<std::thread::id, std::pair<Group, std::function<void(std::shared_ptr<const DataType>)>>> subscription_callbacks_;
        // threads that are subscribed to a given group
        static std::unordered_multimap<Group, typename decltype(subscription_callbacks_)::const_iterator> subscription_groups_;
        // data for a given thread
        static std::unordered_multimap<std::thread::id, std::pair<Group, std::shared_ptr<const DataType>>> data_;
        
        // condition variable to use for data
        static std::unordered_map<std::thread::id, std::shared_ptr<std::condition_variable_any>> data_condition_;
        
    };        
        
    template<typename DataType>
        std::unordered_multimap<std::thread::id, std::pair<Group, std::function<void(std::shared_ptr<const DataType>)>>> SubscriptionStore<DataType>::subscription_callbacks_;
    template<typename DataType>
        std::unordered_multimap<std::thread::id, std::pair<Group, std::shared_ptr<const DataType>>> SubscriptionStore<DataType>::data_;            
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
            std::cout << "IntraProcessTransporter shared_ptr publish" << std::endl;
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
