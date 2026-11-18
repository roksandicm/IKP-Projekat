#pragma once
#include <stddef.h>
#include <stdint.h>
namespace ikp
{
    enum class MessageType : uint32_t
    {
        Query = 1,
        Result = 2,
        Update = 3
    };
    enum class Scope : uint32_t
    {
        Country = 0,
        Region = 1,
        Station = 2
    };
    enum class Status : uint32_t
    {
        Ok = 0,
        Partial = 1,
        NotFound = 2,
        Failed = 3
    };
    struct Message
    {
        MessageType type = MessageType::Query;
        uint64_t request_id = 0;
        uint32_t sender_id = 0;
        Scope scope = Scope::Country;
        uint32_t target_id = 0;
        uint64_t cutoff = 0;
        uint64_t registered = 0;
        uint64_t voted = 0;
        Status status = Status::Ok;
    };
    constexpr size_t WireSize = 64;
    // Fixed-width, big-endian encoding. Never transmit a native C++ struct.
    bool encode(const Message &message, unsigned char (&wire)[WireSize]);
    bool decode(const unsigned char (&wire)[WireSize], Message &message);
    bool valid_message(const Message &message);
} // namespace ikp
