#include "ikp/topology.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ws2tcpip.h>
namespace ikp
{
    static bool fail(char *error, size_t size, const char *text)
    {
        if (error && size)
            snprintf(error, size, "%s", text);
        return false;
    }
    static bool number(const char *text, uint32_t &value)
    {
        if (!text || !*text || *text == '-' || *text == '+')
            return false;
        for (const char *digit = text; *digit; ++digit)
            if (*digit < '0' || *digit > '9')
                return false;
        char *end = nullptr;
        errno = 0;
        unsigned long long n = strtoull(text, &end, 10);
        if (errno || *end || n > UINT32_MAX)
            return false;
        value = static_cast<uint32_t>(n);
        return true;
    }
    bool Topology::load(const char *path, char *error, size_t error_size)
    {
        nodes_.clear();
        root_id_ = 0;
        FILE *file = fopen(path, "r");
        if (!file)
            return fail(error, error_size, "Cannot open topology file.");
        char line[512];
        bool ok = true;
        while (ok && fgets(line, sizeof(line), file))
        {
            if (!strchr(line, '\n') && !feof(file))
            {
                ok = false;
                break;
            }
            char *comment = strchr(line, '#');
            if (comment)
                *comment = 0;
            char *tokens[7] = {};
            unsigned count = 0;
            char *token = strtok(line, " \t\r\n");
            while (token && count < 7)
            {
                tokens[count++] = token;
                token = strtok(nullptr, " \t\r\n");
            }
            if (!count)
                continue;
            if (count != 6)
            {
                ok = false;
                break;
            }
            NodeConfig node;
            uint32_t port;
            if (!number(tokens[0], node.id) || !node.id || node.id == UINT32_MAX ||
                !number(tokens[1], node.parent_id) || !number(tokens[2], node.region_id) ||
                !number(tokens[5], port) || !port || port > 65535 || strlen(tokens[4]) >= sizeof(node.host))
            {
                ok = false;
                break;
            }
            if (!strcmp(tokens[3], "agr"))
                node.kind = NodeKind::Aggregator;
            else if (!strcmp(tokens[3], "station"))
                node.kind = NodeKind::Station;
            else
            {
                ok = false;
                break;
            }
            in_addr parsed;
            if (inet_pton(AF_INET, tokens[4], &parsed) != 1 || parsed.s_addr == INADDR_ANY)
            {
                ok = false;
                break;
            }
            strcpy(node.host, tokens[4]);
            node.port = static_cast<uint16_t>(port);
            if (!nodes_.insert(node.id, node))
                ok = false;
        }
        if (ferror(file))
            ok = false;
        fclose(file);
        if (ok)
            ok = validate(error, error_size);
        else
            fail(error, error_size, "Invalid topology row, duplicate ID, or allocation failure.");
        if (!ok)
        {
            nodes_.clear();
            root_id_ = 0;
        }
        else if (error && error_size)
            error[0] = 0;
        return ok;
    }
    bool Topology::validate(char *error, size_t error_size)
    {
        unsigned roots = 0;
        bool ok = nodes_.size() > 0;
        nodes_.for_each([&](uint64_t, const NodeConfig &node)
                        {
        if (!node.parent_id)
        {
            ++roots;
            root_id_ = node.id;
            if (node.kind != NodeKind::Aggregator)
                ok = false;
        }
        else
        {
            const NodeConfig *parent = find(node.parent_id);
            if (!parent || parent->kind != NodeKind::Aggregator)
                ok = false;
        }
        const NodeConfig *current = &node;
        size_t depth = 0;
        while (current && current->parent_id && depth <= size())
        {
            current = find(current->parent_id);
            ++depth;
        }
        if (!current || depth > size())
            ok = false;
        nodes_.for_each([&](uint64_t, const NodeConfig &other) {
            if (other.id != node.id && other.port == node.port && !strcmp(other.host, node.host))
                ok = false;
        }); });
        if (!ok || roots != 1)
            return fail(error, error_size,
                        "Topology must be one acyclic tree with unique endpoints and aggregator parents.");
        return true;
    }
    bool Topology::descendant(uint32_t node, uint32_t ancestor) const
    {
        const NodeConfig *current = find(node);
        for (size_t depth = 0; current && depth < size(); ++depth)
        {
            if (current->id == ancestor)
                return true;
            current = find(current->parent_id);
        }
        return false;
    }
    bool Topology::covers(uint32_t child, const Message &query) const
    {
        if (!find(child) || query.type != MessageType::Query || !valid_message(query))
            return false;
        if (query.scope == Scope::Country)
            return true;
        if (query.scope == Scope::Station)
        {
            const NodeConfig *target = find(query.target_id);
            return target && target->kind == NodeKind::Station && descendant(target->id, child);
        }
        bool selected = false;
        nodes_.for_each([&](uint64_t, const NodeConfig &target)
                        {
        if (target.region_id == query.target_id &&
            (descendant(target.id, child) || descendant(child, target.id)))
            selected = true; });
        return selected;
    }
} // namespace ikp
