#pragma once
#include <new>
#include <stddef.h>
#include <stdint.h>

namespace ikp
{
    // Separate chaining. Values are copied. Callers synchronize shared access.
    // References remain valid across rehash, until erase/clear/destruction.
    template <class Value>
    class HashMap
    {
        struct Entry
        {
            uint64_t key;
            Value value;
            Entry *next;
            Entry(uint64_t k, const Value &v, Entry *n) : key(k), value(v), next(n)
            {
            }
        };
        Entry **buckets_ = nullptr;
        size_t capacity_ = 0;
        size_t size_ = 0;
        static size_t bucket(uint64_t key, size_t capacity)
        {
            key ^= key >> 30;
            key *= UINT64_C(0xbf58476d1ce4e5b9);
            key ^= key >> 27;
            key *= UINT64_C(0x94d049bb133111eb);
            key ^= key >> 31;
            return static_cast<size_t>(key % capacity);
        }
        bool rehash(size_t capacity)
        {
            Entry **next = new (std::nothrow) Entry *[capacity]();
            if (!next)
                return false;
            for (size_t i = 0; i < capacity_; ++i)
            {
                Entry *entry = buckets_[i];
                while (entry)
                {
                    Entry *following = entry->next;
                    size_t index = bucket(entry->key, capacity);
                    entry->next = next[index];
                    next[index] = entry;
                    entry = following;
                }
            }
            delete[] buckets_;
            buckets_ = next;
            capacity_ = capacity;
            return true;
        }

    public:
        HashMap() = default;
        ~HashMap()
        {
            clear();
            delete[] buckets_;
        }
        HashMap(const HashMap &) = delete;
        HashMap &operator=(const HashMap &) = delete;
        Value *find(uint64_t key)
        {
            if (!capacity_)
                return nullptr;
            for (Entry *e = buckets_[bucket(key, capacity_)]; e; e = e->next)
                if (e->key == key)
                    return &e->value;
            return nullptr;
        }
        const Value *find(uint64_t key) const
        {
            return const_cast<HashMap *>(this)->find(key);
        }
        bool insert(uint64_t key, const Value &value)
        {
            if (find(key))
                return false;
            if (!capacity_ && !rehash(16))
                return false;
            if (size_ >= capacity_ - capacity_ / 4)
            {
                if (capacity_ > SIZE_MAX / 2 || !rehash(capacity_ * 2))
                    return false;
            }
            size_t index = bucket(key, capacity_);
            Entry *entry = new (std::nothrow) Entry(key, value, buckets_[index]);
            if (!entry)
                return false;
            buckets_[index] = entry;
            ++size_;
            return true;
        }
        bool erase(uint64_t key)
        {
            if (!capacity_)
                return false;
            Entry **link = &buckets_[bucket(key, capacity_)];
            while (*link)
            {
                Entry *entry = *link;
                if (entry->key == key)
                {
                    *link = entry->next;
                    delete entry;
                    --size_;
                    return true;
                }
                link = &entry->next;
            }
            return false;
        }
        void clear()
        {
            for (size_t i = 0; i < capacity_; ++i)
            {
                while (buckets_[i])
                {
                    Entry *entry = buckets_[i];
                    buckets_[i] = entry->next;
                    delete entry;
                }
            }
            size_ = 0;
        }
        size_t size() const
        {
            return size_;
        }
        template <class Visitor>
        void for_each(Visitor visitor) const
        {
            for (size_t i = 0; i < capacity_; ++i)
                for (Entry *e = buckets_[i]; e; e = e->next)
                    visitor(e->key, static_cast<const Value &>(e->value));
        }
    };
} // namespace ikp
