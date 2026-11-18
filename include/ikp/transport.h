#pragma once
#include "ikp/network.h"
#include "ikp/outbound_queue.h"
#include "ikp/topology.h"
namespace ikp
{
    enum class Origin
    {
        Parent,
        Child
    };
    struct ReceivedMessage
    {
        Message message;
        Origin origin = Origin::Parent;
        uint32_t peer_id = 0;
    };
    // Student 2 implements this with the synchronized circular receive buffer.
    class InboundSink
    {
    public:
        virtual ~InboundSink() = default;
        virtual bool push(const ReceivedMessage &message) = 0;
        // Must wake blocked push/pop operations. Called on final transport shutdown.
        virtual void close() = 0;
    };
    struct ChildConnection
    {
        NodeConfig config;
        SOCKET socket = INVALID_SOCKET;
    };
    class Transport
    {
        const Topology &topology_;
        uint32_t node_id_;
        InboundSink &inbound_;
        HashMap<ChildConnection> children_;
        OutboundQueue outgoing_;
        SOCKET parent_ = INVALID_SOCKET;
        HANDLE tp_ = nullptr;
        HANDLE ts_ = nullptr;
        volatile LONG stopping_ = 0;
        volatile LONG failures_ = 0;
        volatile LONG parent_closed_ = 0;
        bool used_ = false;
        static DWORD WINAPI receive_entry(void *context);
        static DWORD WINAPI send_entry(void *context);
        void receive_parent();
        void send_loop();

    public:
        Transport(const Topology &topology, uint32_t node_id, InboundSink &inbound, size_t queue_limit = 4096);
        ~Transport();
        Transport(const Transport &) = delete;
        Transport &operator=(const Transport &) = delete;
        // Bootstrap before starting TC/TOZ. Connect children bottom-up.
        bool connect_children(DWORD timeout_ms = 3000);
        // Listen on this node's configured endpoint, accept one parent, then start TP/TS.
        bool accept_and_start(DWORD timeout_ms = 30000);
        // Takes ownership only on success. Sets the parent socket nonblocking.
        // Parent identity is configured out of band; root expects source ID UINT32_MAX.
        bool start(SOCKET parent);
        // Only TS sends frames. Workers enqueue and retain ownership of their messages.
        bool enqueue_child(uint32_t child_id, const Message &message);
        bool enqueue_parent(const Message &message);
        const ChildConnection *child(uint32_t id) const
        {
            return children_.find(id);
        }
        template <class Visitor>
        void for_each_child(Visitor visitor) const
        {
            children_.for_each(visitor);
        }
        uint32_t node_id() const
        {
            return node_id_;
        }
        LONG failures()
        {
            return InterlockedCompareExchange(&failures_, 0, 0);
        }
        bool parent_closed()
        {
            return InterlockedCompareExchange(&parent_closed_, 0, 0) != 0;
        }
        // TC/TOZ/pool must be joined by their owner before destruction.
        // stop wakes inbound waiters, drains TS, shuts sockets, and joins TP/TS.
        // Socket handles remain allocated until destruction so external threads can finish safely.
        // Stop producers, stop transport, join TC/TOZ/pool, then destroy transport.
        void stop();
    };
} // namespace ikp
