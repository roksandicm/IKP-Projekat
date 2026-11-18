#pragma once
#include "ikp/protocol.h"
// Winsock 2 must precede windows.h to avoid legacy winsock.h declarations.
// clang-format off
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
// clang-format on
namespace ikp
{
    class Winsock
    {
        bool ready_;

    public:
        Winsock();
        ~Winsock();
        bool ready() const
        {
            return ready_;
        }
        Winsock(const Winsock &) = delete;
        Winsock &operator=(const Winsock &) = delete;
    };
    // IPv4 numeric addresses. Every I/O operation has a total deadline.
    SOCKET connect_tcp(const char *host, uint16_t port, DWORD timeout_ms = 3000);
    SOCKET listen_tcp(const char *host, uint16_t port);
    SOCKET accept_tcp(SOCKET listener, DWORD timeout_ms = 3000);
    bool send_message(SOCKET socket, const Message &message, DWORD timeout_ms = 3000);
    bool receive_message(SOCKET socket, Message &message, DWORD timeout_ms = 3000,
                         volatile LONG *cancel = nullptr);
    void close_socket(SOCKET &socket);
} // namespace ikp
