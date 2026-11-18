#include "ikp/protocol.h"
namespace ikp
{
    static void put32(unsigned char *p, uint32_t n)
    {
        for (int i = 3; i >= 0; --i)
        {
            p[i] = static_cast<unsigned char>(n);
            n >>= 8;
        }
    }
    static uint32_t get32(const unsigned char *p)
    {
        uint32_t n = 0;
        for (int i = 0; i < 4; ++i)
            n = (n << 8) | p[i];
        return n;
    }
    static void put64(unsigned char *p, uint64_t n)
    {
        put32(p, static_cast<uint32_t>(n >> 32));
        put32(p + 4, static_cast<uint32_t>(n));
    }
    static uint64_t get64(const unsigned char *p)
    {
        return (static_cast<uint64_t>(get32(p)) << 32) | get32(p + 4);
    }
    bool valid_message(const Message &m)
    {
        if (m.type < MessageType::Query || m.type > MessageType::Update || m.scope > Scope::Station ||
            m.status > Status::Failed || !m.sender_id)
            return false;
        if ((m.scope == Scope::Country) != (m.target_id == 0))
            return false;
        if (m.type == MessageType::Update)
        {
            if (m.request_id || m.status != Status::Ok)
                return false;
        }
        else if (!m.request_id)
            return false;
        if (m.voted > m.registered)
            return false;
        if (m.type == MessageType::Query && (m.registered || m.voted || m.status != Status::Ok))
            return false;
        return true;
    }
    bool encode(const Message &m, unsigned char (&wire)[WireSize])
    {
        if (!valid_message(m))
            return false;
        put32(wire, 0x494b5035);
        put32(wire + 4, 1);
        put32(wire + 8, static_cast<uint32_t>(m.type));
        put64(wire + 12, m.request_id);
        put32(wire + 20, m.sender_id);
        put32(wire + 24, static_cast<uint32_t>(m.scope));
        put32(wire + 28, m.target_id);
        put64(wire + 32, m.cutoff);
        put64(wire + 40, m.registered);
        put64(wire + 48, m.voted);
        put32(wire + 56, static_cast<uint32_t>(m.status));
        put32(wire + 60, 0);
        return true;
    }
    bool decode(const unsigned char (&wire)[WireSize], Message &message)
    {
        if (get32(wire) != 0x494b5035 || get32(wire + 4) != 1 || get32(wire + 60))
            return false;
        Message m;
        m.type = static_cast<MessageType>(get32(wire + 8));
        m.request_id = get64(wire + 12);
        m.sender_id = get32(wire + 20);
        m.scope = static_cast<Scope>(get32(wire + 24));
        m.target_id = get32(wire + 28);
        m.cutoff = get64(wire + 32);
        m.registered = get64(wire + 40);
        m.voted = get64(wire + 48);
        m.status = static_cast<Status>(get32(wire + 56));
        if (!valid_message(m))
            return false;
        message = m;
        return true;
    }
} // namespace ikp
