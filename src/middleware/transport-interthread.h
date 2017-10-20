#ifndef TransportInterThread20160609H
#define TransportInterThread20160609H

#include <thread>
#include <typeindex>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <set>
#include <atomic>

#include "transport-common.h"

namespace goby
{
    // increments and decrements a reader count using RAII
    // once the reader count goes to zero, notifies a condition variable (for the writer(s) to use)
    template<typename ConditionVariable>
        struct ReaderRegister
        {
        ReaderRegister(std::atomic<int>& counter, ConditionVariable& cv) : counter_(counter), cv_(cv)
                { ++counter; }
            ~ReaderRegister()
                {
                    --counter_;
                    if(counter_ == 0) cv_.notify_all();
                }
            std::atomic<int>& counter_;
            ConditionVariable& cv_;
        };
    
    
    class SubscriptionStoreBase
    {
    public:
        // returns number of data items posted to callbacks 
        static int poll_all(std::thread::id thread_id, const std::chrono::system_clock::time_point& timeout_time)
        {
            if(stores_mutex_.try_lock_until(timeout_time))
            {
                // multiple readers
                ReaderRegister<decltype(stores_cv_)>(pollers_, stores_cv_);
                stores_mutex_.unlock();
                
                int poll_items = 0;
                for (auto const &s : stores_)
                    poll_items += s.second->poll(thread_id, timeout_time);
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
            while(pollers_ > 0) // wait for readers
                stores_cv_.wait(stores_mutex_);
            
            auto index = std::type_index(typeid(StoreType));
            if(!stores_.count(index))
                stores_.insert(std::make_pair(index, std::unique_ptr<StoreType>(new StoreType)));
        }
        
    protected:
        virtual int poll(std::thread::id thread_id, const std::chrono::system_clock::time_point& timeout_time) = 0;
        

    private:
        // stores a map of Datas to SubscriptionStores so that can call poll() on all the stores
        static std::unordered_map<std::type_index, std::unique_ptr<SubscriptionStoreBase>> stores_;
        static std::timed_mutex stores_mutex_;
        static std::condition_variable_any stores_cv_;
        static std::atomic<int> pollers_; // number of active readers on stores_mutex_
    };

    
    template<typename Data>
        class SubscriptionStore : public SubscriptionStoreBase
    {
    public:
        static void subscribe(std::function<void(std::shared_ptr<const Data>)> func, const Group& group, std::thread::id thread_id, std::shared_ptr<std::mutex> mutex, std::shared_ptr<std::condition_variable_any> cv)
        {
            {
                std::unique_lock<std::mutex> lock(subscription_mutex_);
                while(subscription_readers_ > 0)
                    subscription_cv_.wait(lock);
                
                // insert callback
                auto it = subscription_callbacks_.insert(std::make_pair(thread_id, Callback(group, func)));
                // insert group with iterator to callback
                subscription_groups_.insert(std::make_pair(group, it));

                // if necessary, create a DataQueue for this thread
                auto queue_it = data_.find(thread_id);
                if(queue_it == data_.end())
                {
                    auto bool_it_pair = data_.insert(std::make_pair(thread_id, DataQueue()));
                    queue_it = bool_it_pair.first;
                }
                queue_it->second.create(group);
                
                
                // if we don't have a condition variable already for this thread, store it
                if(!data_protection_.count(thread_id))
                    data_protection_.insert(std::make_pair(thread_id, std::make_pair(mutex, cv)));
            }
            
            // try inserting a copy of this templated class via the base class for SubscriptionStoreBase::poll_all to use
            SubscriptionStoreBase::insert<SubscriptionStore<Data>>();
        }

        static void publish(std::shared_ptr<const Data> data, const Group& group, const goby::protobuf::TransporterConfig& transport_cfg)
        {
            // push new data
            // build up local vector of relevant condition variables while locked
            std::vector<std::pair<std::shared_ptr<std::mutex>, std::shared_ptr<std::condition_variable_any>>> cv_to_notify;
            {
                { std::lock_guard<decltype(subscription_mutex_)> lock(subscription_mutex_); }

                {
                    ReaderRegister<decltype(subscription_cv_)>(subscription_readers_, subscription_cv_);
                
                    auto range = subscription_groups_.equal_range(group);
                    for (auto it = range.first; it != range.second; ++it)
                    {
                        std::thread::id thread_id = it->second->first;
                    
                        // don't store a copy if publisher == subscriber, and echo is false
                        if(thread_id != std::this_thread::get_id() || transport_cfg.echo())
                        {
                            // protect the DataQueue we are writing to
                            std::unique_lock<std::mutex> lock(*(data_protection_.find(thread_id)->second.first));
                            auto queue_it = data_.find(thread_id);
                            queue_it->second.insert(group, data);
                            cv_to_notify.push_back(data_protection_.at(thread_id));
                        }
                    }
                }
            }

            // unlock and notify condition variables from local vector
            for (const auto& cv_mutex_pair : cv_to_notify)
            {
                cv_mutex_pair.second->notify_all();
            }
        }
        
        

    private:
        int poll(std::thread::id thread_id, const std::chrono::system_clock::time_point& timeout_time)
        {

            std::vector<std::pair<std::function<void(std::shared_ptr<const Data>)>, std::shared_ptr<const Data>>> data_callbacks;
            int poll_items_count = 0;

            { std::lock_guard<decltype(subscription_mutex_)> lock(subscription_mutex_); }

            {
                ReaderRegister<decltype(subscription_cv_)>(subscription_readers_, subscription_cv_);

            
                auto queue_it = data_.find(thread_id);
                if(queue_it == data_.end())
                    return 0; // no subscriptions
            

                {
                    std::unique_lock<std::mutex> lock(*(data_protection_.find(thread_id)->second.first));
                                
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
                            // store the callback function and datum for all the elements queued
                            for(auto& datum : data_it->second)
                            {
                                ++poll_items_count;
                                data_callbacks.push_back(std::make_pair(callback, datum));
                            }
                        }
                        queue_it->second.clear(group);
                    }
                }
            }
            
            // now that we're no longer blocking the data mutex, actually run the callbacks
            for (const auto& callback_datum_pair : data_callbacks)
                callback_datum_pair.first(callback_datum_pair.second);
            
            return poll_items_count;
        }
            
    private:
        struct Callback
        {
        Callback(const Group& g, const std::function<void(std::shared_ptr<const Data>)>& c) : group(g), callback(c) {}
            Group group;
            std::function<void(std::shared_ptr<const Data>)> callback;
        };
        
        class DataQueue
        {
        private:
            std::unordered_map<Group, std::vector<std::shared_ptr<const Data>>> data_;
        public:
            void create(const Group& g)
            {
                auto it = data_.find(g);
                if(it == data_.end())
                    data_.insert(std::make_pair(g, std::vector<std::shared_ptr<const Data>>()));
            }            
            void insert(const Group& g, std::shared_ptr<const Data> datum)
            {
                data_.find(g)->second.push_back(datum);
            }
            void clear(const Group& g)
            { data_.find(g)->second.clear(); }
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
        static std::unordered_map<std::thread::id, std::pair<std::shared_ptr<std::mutex>, std::shared_ptr<std::condition_variable_any>>> data_protection_;

        static std::mutex subscription_mutex_; // protects subscription_callbacks, subscription_groups, data_protection, and the overarching data_ map (but not the DataQueues within it, which are protected by the mutexes stored in data_protection_))
        static std::condition_variable subscription_cv_;
        static std::atomic<int> subscription_readers_; // number of active readers on subscription_mutex_

        
        // data for a given thread
        static std::unordered_map<std::thread::id, DataQueue> data_;
        
        
    };        
        
    template<typename Data>
        std::unordered_multimap<std::thread::id, typename SubscriptionStore<Data>::Callback> SubscriptionStore<Data>::subscription_callbacks_;
    template<typename Data>
        std::unordered_map<std::thread::id, typename SubscriptionStore<Data>::DataQueue> SubscriptionStore<Data>::data_;            
    template<typename Data>
        std::unordered_multimap<goby::Group, typename decltype(SubscriptionStore<Data>::subscription_callbacks_)::const_iterator> SubscriptionStore<Data>::subscription_groups_;
    template<typename Data>
        std::unordered_map<std::thread::id, std::pair<std::shared_ptr<std::mutex>, std::shared_ptr<std::condition_variable_any>>> SubscriptionStore<Data>::data_protection_;

    template<typename Data>
        std::mutex SubscriptionStore<Data>::subscription_mutex_;
    template<typename Data>
        std::condition_variable SubscriptionStore<Data>::subscription_cv_;
    template<typename Data>
        std::atomic<int> SubscriptionStore<Data>::subscription_readers_;  

    class InterThreadTransporter :
        public StaticTransporterInterface<InterThreadTransporter, NullTransporter>,
        public PollAbsoluteTimeInterface<InterThreadTransporter>
    {
    public:


    InterThreadTransporter() :
        data_mutex_(std::make_shared<std::mutex>()),
            poll_mutex_(std::make_shared<std::timed_mutex>()),
            cv_(std::make_shared<std::condition_variable_any>())
        { }

	template<typename Data>
	    static constexpr int scheme()
	{
	    return MarshallingScheme::CXX_OBJECT;
	}
	
        template<typename Data, int scheme = scheme<Data>()>
            void publish_dynamic(const Data& data, const Group& group, const goby::protobuf::TransporterConfig& transport_cfg = goby::protobuf::TransporterConfig())
        {
            check_validity_runtime(group);
            std::shared_ptr<Data> data_ptr(new Data(data));
            publish_dynamic<Data>(data_ptr, group, transport_cfg);
        }
        
        template<typename Data, int scheme = scheme<Data>()>
            void publish_dynamic(std::shared_ptr<const Data> data, const Group& group, const goby::protobuf::TransporterConfig& transport_cfg = goby::protobuf::TransporterConfig())
        {
            check_validity_runtime(group);
            SubscriptionStore<Data>::publish(data, group, transport_cfg);
        }

        template<typename Data, int scheme = scheme<Data>()>
            void publish_dynamic(std::shared_ptr<Data> data, const Group& group, const goby::protobuf::TransporterConfig& transport_cfg = goby::protobuf::TransporterConfig())
            { publish_dynamic<Data, scheme>(std::shared_ptr<const Data>(data), group, transport_cfg); }


        template<typename Data, int scheme = scheme<Data>()>
            void subscribe_dynamic(std::function<void(const Data&)> f, const Group& group)
        {
            check_validity_runtime(group);
            SubscriptionStore<Data>::subscribe([=](std::shared_ptr<const Data> pd) { f(*pd); }, group, std::this_thread::get_id(), data_mutex_, cv_);
        }
        
        template<typename Data, int scheme = scheme<Data>()>
            void subscribe_dynamic(std::function<void(std::shared_ptr<const Data>)> f, const Group& group)
        {
            check_validity_runtime(group);
            SubscriptionStore<Data>::subscribe(f, group, std::this_thread::get_id(), data_mutex_, cv_);
        }


	friend PollAbsoluteTimeInterface<InterThreadTransporter>;
    private:	

	int _poll(const std::chrono::system_clock::time_point& timeout = std::chrono::system_clock::time_point::max())
        {
            std::thread::id thread_id = std::this_thread::get_id();
            std::unique_lock<std::timed_mutex> lock(*poll_mutex_);
            int poll_items = SubscriptionStoreBase::poll_all(thread_id, timeout);

            while(poll_items == 0) // no items, so wait
            {
                if(timeout == std::chrono::system_clock::time_point::max())
                {
                    cv_->wait(lock); // wait_until doesn't work well with time_point::max()
                    poll_items = SubscriptionStoreBase::poll_all(thread_id, timeout);
                }
                else
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
            }

            return poll_items;
        }
        
    private:
        // protects this thread's DataQueue
        std::shared_ptr<std::mutex> data_mutex_;
        // doesn't protect anything, used for convenient signalling through the condition variable
        std::shared_ptr<std::timed_mutex> poll_mutex_;
        // signaled when there's no data for this thread to read during _poll()
        std::shared_ptr<std::condition_variable_any> cv_;
    };
    
}

#endif
