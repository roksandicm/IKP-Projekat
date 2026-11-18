#include "ikp/transport.h"
namespace ikp
{
    Transport::Transport(const Topology &topology, uint32_t node_id, InboundSink &inbound, size_t limit)
        : topology_(topology), node_id_(node_id), inbound_(inbound), outgoing_(limit)
    {
    }
    Transport::~Transport()
    {
        stop();
        close_socket(parent_);
        children_.for_each([](uint64_t, const ChildConnection &child)
                           {
        SOCKET socket = child.socket;
        close_socket(socket); });
    }
    bool Transport::connect_children(DWORD timeout_ms)
    {
        if (used_ || InterlockedCompareExchange(&stopping_, 0, 0) || children_.size())
            return false;
        const NodeConfig *self = topology_.find(node_id_);
        if (!self || self->kind != NodeKind::Aggregator)
            return false;
        bool ok = true;
        topology_.for_each([&](uint64_t, const NodeConfig &node)
                           {
        if (!ok || node.parent_id != node_id_)
            return;
        ChildConnection child;
        child.config = node;
        child.socket = connect_tcp(node.host, node.port, timeout_ms);
        if (child.socket == INVALID_SOCKET)
        {
            ok = false;
            return;
        }
        if (!children_.insert(node.id, child))
        {
            close_socket(child.socket);
            ok = false;
        } });
        if (!ok)
        {
            children_.for_each([](uint64_t, const ChildConnection &child)
                               {
            SOCKET socket = child.socket;
            close_socket(socket); });
            children_.clear();
        }
        return ok;
    }
    bool Transport::accept_and_start(DWORD timeout_ms)
    {
        const NodeConfig *self = topology_.find(node_id_);
        if (!self || used_ || InterlockedCompareExchange(&stopping_, 0, 0))
            return false;
        SOCKET listener = listen_tcp(self->host, self->port);
        if (listener == INVALID_SOCKET)
            return false;
        SOCKET parent = accept_tcp(listener, timeout_ms);
        close_socket(listener);
        if (parent == INVALID_SOCKET)
            return false;
        if (start(parent))
            return true;
        close_socket(parent);
        return false;
    }
    bool Transport::start(SOCKET parent)
    {
        if (used_ || InterlockedCompareExchange(&stopping_, 0, 0) || parent == INVALID_SOCKET)
            return false;
        const NodeConfig *self = topology_.find(node_id_);
        if (!self || self->kind != NodeKind::Aggregator)
            return false;
        size_t expected = 0;
        topology_.for_each([&](uint64_t, const NodeConfig &node)
                           {
        if (node.parent_id == node_id_)
            ++expected; });
        if (children_.size() != expected)
            return false;
        u_long nonblocking = 1;
        if (ioctlsocket(parent, FIONBIO, &nonblocking))
            return false;
        used_ = true;
        parent_ = parent;
        ts_ = CreateThread(nullptr, 0, send_entry, this, 0, nullptr);
        if (ts_)
            tp_ = CreateThread(nullptr, 0, receive_entry, this, 0, nullptr);
        if (tp_)
            return true;
        outgoing_.close();
        if (ts_)
        {
            WaitForSingleObject(ts_, INFINITE);
            CloseHandle(ts_);
            ts_ = nullptr;
        }
        parent_ = INVALID_SOCKET;
        return false;
    }
    DWORD WINAPI Transport::receive_entry(void *context)
    {
        static_cast<Transport *>(context)->receive_parent();
        return 0;
    }
    DWORD WINAPI Transport::send_entry(void *context)
    {
        static_cast<Transport *>(context)->send_loop();
        return 0;
    }
    void Transport::receive_parent()
    {
        const NodeConfig *self = topology_.find(node_id_);
        const uint32_t expected = self->parent_id ? self->parent_id : UINT32_MAX;
        while (!InterlockedCompareExchange(&stopping_, 0, 0))
        {
            // Idle connections remain open. Once a frame starts it has a finite deadline.
            fd_set readers;
            FD_ZERO(&readers);
            FD_SET(parent_, &readers);
            timeval timeout{0, 100000};
            int ready = select(0, &readers, nullptr, nullptr, &timeout);
            if (ready == 0)
                continue;
            Message message;
            if (ready == SOCKET_ERROR || !receive_message(parent_, message, 3000, &stopping_) ||
                message.type != MessageType::Query || message.sender_id != expected)
                break;
            if (!inbound_.push(ReceivedMessage{message, Origin::Parent, expected}))
                break;
        }
        InterlockedExchange(&parent_closed_, 1);
    }
    void Transport::send_loop()
    {
        OutboundMessage item;
        while (outgoing_.pop(item))
        {
            if (!send_message(item.socket, item.message))
            {
                InterlockedIncrement(&failures_);
                // A partial frame makes the stream unusable. Keep the handle until destruction.
                shutdown(item.socket, SD_BOTH);
            }
        }
    }
    bool Transport::enqueue_child(uint32_t child_id, const Message &message)
    {
        const ChildConnection *selected = child(child_id);
        if (!used_ || InterlockedCompareExchange(&stopping_, 0, 0) || !selected ||
            message.type != MessageType::Query || !valid_message(message))
            return false;
        Message forwarded = message;
        forwarded.sender_id = node_id_;
        return outgoing_.push(OutboundMessage{selected->socket, forwarded});
    }
    bool Transport::enqueue_parent(const Message &message)
    {
        if (!used_ || InterlockedCompareExchange(&stopping_, 0, 0) || message.type == MessageType::Query ||
            !valid_message(message))
            return false;
        Message forwarded = message;
        forwarded.sender_id = node_id_;
        return outgoing_.push(OutboundMessage{parent_, forwarded});
    }
    void Transport::stop()
    {
        if (InterlockedExchange(&stopping_, 1))
            return;
        inbound_.close();
        outgoing_.close();
        // Cancel the read deadline and wake TP without closing handles used by workers.
        if (parent_ != INVALID_SOCKET)
            shutdown(parent_, SD_RECEIVE);
        if (tp_)
        {
            WaitForSingleObject(tp_, INFINITE);
            CloseHandle(tp_);
            tp_ = nullptr;
        }
        if (ts_)
        {
            WaitForSingleObject(ts_, INFINITE);
            CloseHandle(ts_);
            ts_ = nullptr;
        }
        if (parent_ != INVALID_SOCKET)
            shutdown(parent_, SD_BOTH);
        children_.for_each([](uint64_t, const ChildConnection &child)
                           { shutdown(child.socket, SD_BOTH); });
    }
} // namespace ikp
