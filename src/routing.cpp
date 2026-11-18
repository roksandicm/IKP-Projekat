#include "ikp/routing.h"
#include <new>
namespace ikp
{
    bool RoutePlan::build(const Topology &topology, uint32_t self_id, const Message &query)
    {
        delete[] ids_;
        ids_ = nullptr;
        count_ = 0;
        valid_ = false;
        const NodeConfig *self = topology.find(self_id);
        if (!self || self->kind != NodeKind::Aggregator || query.type != MessageType::Query ||
            !valid_message(query))
            return false;
        size_t capacity = 0;
        topology.for_each([&](uint64_t, const NodeConfig &child)
                          {
        if (child.parent_id == self_id && topology.covers(child.id, query))
            ++capacity; });
        self_id_ = self_id;
        query_ = query;
        if (!capacity)
        {
            valid_ = true;
            return true;
        }
        ids_ = new (std::nothrow) uint32_t[capacity];
        if (!ids_)
            return false;
        topology.for_each([&](uint64_t, const NodeConfig &child)
                          {
        if (child.parent_id == self_id && topology.covers(child.id, query))
            ids_[count_++] = child.id; });
        valid_ = true;
        return true;
    }
    size_t RoutePlan::dispatch(Transport &transport, const Message &query) const
    {
        if (!valid_ || transport.node_id() != self_id_ || query.type != MessageType::Query ||
            query.request_id != query_.request_id || query.scope != query_.scope ||
            query.target_id != query_.target_id || query.cutoff != query_.cutoff)
            return 0;
        size_t sent = 0;
        for (size_t i = 0; i < count_; ++i)
            if (transport.enqueue_child(ids_[i], query))
                ++sent;
        return sent;
    }
} // namespace ikp
