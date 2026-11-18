#pragma once
#include "ikp/network.h"
#include <new>
namespace ikp
{
    struct OutboundMessage
    {
        SOCKET socket = INVALID_SOCKET;
        Message message;
    };
    // Bounded linked FIFO. close() wakes all waiters and allows the consumer to drain.
    class OutboundQueue
    {
        struct Entry
        {
            OutboundMessage item;
            Entry *next;
        };
        Entry *head_ = nullptr;
        Entry *tail_ = nullptr;
        size_t size_ = 0;
        const size_t limit_;
        bool closed_ = false;
        CRITICAL_SECTION lock_;
        CONDITION_VARIABLE readable_, writable_;

    public:
        explicit OutboundQueue(size_t limit = 4096) : limit_(limit ? limit : 1)
        {
            InitializeCriticalSection(&lock_);
            InitializeConditionVariable(&readable_);
            InitializeConditionVariable(&writable_);
        }
        ~OutboundQueue()
        {
            close();
            while (head_)
            {
                Entry *entry = head_;
                head_ = head_->next;
                delete entry;
            }
            DeleteCriticalSection(&lock_);
        }
        OutboundQueue(const OutboundQueue &) = delete;
        OutboundQueue &operator=(const OutboundQueue &) = delete;
        bool push(const OutboundMessage &item)
        {
            EnterCriticalSection(&lock_);
            while (size_ == limit_ && !closed_)
                SleepConditionVariableCS(&writable_, &lock_, INFINITE);
            if (closed_)
            {
                LeaveCriticalSection(&lock_);
                return false;
            }
            Entry *entry = new (std::nothrow) Entry{item, nullptr};
            if (!entry)
            {
                LeaveCriticalSection(&lock_);
                return false;
            }
            if (tail_)
                tail_->next = entry;
            else
                head_ = entry;
            tail_ = entry;
            ++size_;
            WakeConditionVariable(&readable_);
            LeaveCriticalSection(&lock_);
            return true;
        }
        bool pop(OutboundMessage &item)
        {
            EnterCriticalSection(&lock_);
            while (!head_ && !closed_)
                SleepConditionVariableCS(&readable_, &lock_, INFINITE);
            if (!head_)
            {
                LeaveCriticalSection(&lock_);
                return false;
            }
            Entry *entry = head_;
            item = entry->item;
            head_ = entry->next;
            if (!head_)
                tail_ = nullptr;
            delete entry;
            --size_;
            WakeConditionVariable(&writable_);
            LeaveCriticalSection(&lock_);
            return true;
        }
        void close()
        {
            EnterCriticalSection(&lock_);
            closed_ = true;
            WakeAllConditionVariable(&readable_);
            WakeAllConditionVariable(&writable_);
            LeaveCriticalSection(&lock_);
        }
    };
} // namespace ikp
