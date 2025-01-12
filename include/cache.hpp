#ifndef CACHE_HPP
#define CACHE_HPP

#include "cache_policy.hpp"

#include <cstddef>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>

namespace caches
{

// Base class for caching algorithms
template <typename Key, typename Value, typename Policy = NoCachePolicy<Key>>
class fixed_sized_cache
{
  public:
    using iterator = typename std::unordered_map<Key, Value>::iterator;
    using const_iterator =
        typename std::unordered_map<Key, Value>::const_iterator;
    using write_guard = typename std::unique_lock<std::shared_mutex>;
    using read_guard = typename std::shared_lock<std::shared_mutex>;
    using Callback =
        typename std::function<void(const Key &key, const Value &value)>;

    fixed_sized_cache(size_t max_size, const Policy &policy = Policy(),
                      Callback OnErase = [](const Key &, const Value &) {})
        : cache_policy(policy), max_cache_size(max_size),
          OnEraseCallback(OnErase)
    {
        if (max_cache_size == 0)
        {
            max_cache_size = std::numeric_limits<size_t>::max();
        }
    }

    ~fixed_sized_cache()
    {
        Clear();
    }

    void Put(const Key &key, const Value &value)
    {
        write_guard lock{safe_op};
        auto elem_it = FindElem(key);

        if (elem_it == cache_items_map.end())
        {
            // add new element to the cache
            if (cache_items_map.size() + 1 > max_cache_size)
            {
                auto disp_candidate_key = cache_policy.ReplCandidate();

                Erase(disp_candidate_key);
            }

            Insert(key, value);
        }
        else
        {
            // update previous value
            Update(key, value);
        }
    }

    std::unique_ptr<Value> TryGet(const Key &key) const
    {
        std::unique_ptr<Value> ret;

        read_guard lock{safe_op};
        auto elem_it = FindElem(key);

        if (elem_it != cache_items_map.end())
        {
            cache_policy.Touch(key);
            ret = std::make_unique<Value>(elem_it->second);
        }

        return ret;
    }

    Value Get(const Key &key) const
    {
        read_guard lock{safe_op};
        auto elem_it = FindElem(key);

        if (elem_it == cache_items_map.end())
        {
            throw std::range_error{"No such element in the cache"};
        }
        cache_policy.Touch(key);

        return elem_it->second;
    }

    bool Cached(const Key &key) const
    {
        read_guard lock{safe_op};
        return FindElem(key) != cache_items_map.end();
    }

    size_t Size() const
    {
        read_guard lock{safe_op};

        return cache_items_map.size();
    }

    size_t size() const
    {
        return Size();
    }

    void Clear()
    {
        write_guard lock{safe_op};

        cache_policy.Clear();

        for (auto it = begin(); it != end(); ++it)
        {
            OnEraseCallback(it->first, it->second);
        }
        cache_items_map.clear();
    }

    typename std::unordered_map<Key, Value>::const_iterator begin() const
    {
        return cache_items_map.begin();
    }

    typename std::unordered_map<Key, Value>::const_iterator end() const
    {
        return cache_items_map.end();
    }

  protected:
    void Insert(const Key &key, const Value &value)
    {
        cache_policy.Insert(key);
        cache_items_map.emplace(key, value);
    }

    void Erase(const Key &key)
    {
        cache_policy.Erase(key);

        auto elem_it = FindElem(key);
        OnEraseCallback(key, elem_it->second);
        cache_items_map.erase(elem_it);
    }

    void Update(const Key &key, const Value &value)
    {
        cache_policy.Touch(key);
        cache_items_map[key] = value;
    }

    const_iterator FindElem(const Key &key) const
    {
        return cache_items_map.find(key);
    }

  private:
    std::unordered_map<Key, Value> cache_items_map;
    mutable Policy cache_policy;
    mutable std::shared_mutex safe_op;
    size_t max_cache_size;
    Callback OnEraseCallback;
};
} // namespace caches

#endif // CACHE_HPP
