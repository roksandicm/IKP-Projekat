#pragma once
#include "ikp/hash_map.h"
#include "ikp/protocol.h"
namespace ikp
{
    enum class NodeKind
    {
        Aggregator,
        Station
    };
    struct NodeConfig
    {
        uint32_t id = 0;
        uint32_t parent_id = 0;
        uint32_t region_id = 0;
        NodeKind kind = NodeKind::Aggregator;
        char host[16] = {};
        uint16_t port = 0;
    };
    class Topology
    {
        HashMap<NodeConfig> nodes_;
        uint32_t root_id_ = 0;
        bool validate(char *error, size_t error_size);

    public:
        // On failure the topology is empty. Load before starting any threads.
        bool load(const char *path, char *error, size_t error_size);
        const NodeConfig *find(uint32_t id) const
        {
            return nodes_.find(id);
        }
        uint32_t root_id() const
        {
            return root_id_;
        }
        size_t size() const
        {
            return nodes_.size();
        }
        bool descendant(uint32_t node, uint32_t ancestor) const;
        bool covers(uint32_t child, const Message &query) const;
        template <class Visitor>
        void for_each(Visitor visitor) const
        {
            nodes_.for_each(visitor);
        }
    };
} // namespace ikp
