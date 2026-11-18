#pragma once
#include "ikp/transport.h"
namespace ikp
{
    // Build plan -> register active request -> dispatch. Register BEFORE dispatch,
    // because a child may answer immediately. Plan must live through dispatch.
    class RoutePlan
    {
        uint32_t *ids_ = nullptr;
        size_t count_ = 0;
        uint32_t self_id_ = 0;
        Message query_;
        bool valid_ = false;

    public:
        ~RoutePlan()
        {
            delete[] ids_;
        }
        RoutePlan() = default;
        RoutePlan(const RoutePlan &) = delete;
        RoutePlan &operator=(const RoutePlan &) = delete;
        bool build(const Topology &topology, uint32_t self_id, const Message &query);
        size_t count() const
        {
            return count_;
        }
        uint32_t child_id(size_t index) const
        {
            return index < count_ ? ids_[index] : 0;
        }
        // Failed enqueues are returned to the caller. It must finish as Failed/Partial.
        size_t dispatch(Transport &transport, const Message &query) const;
    };
} // namespace ikp
