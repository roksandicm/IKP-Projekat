#include "ikp/network.h"
#include <limits.h>
namespace ikp
{
    Winsock::Winsock()
    {
        WSADATA data;
        ready_ = WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }
    Winsock::~Winsock()
    {
        if (ready_)
            WSACleanup();
    }
    void close_socket(SOCKET &socket)
    {
        if (socket != INVALID_SOCKET)
        {
            shutdown(socket, SD_BOTH);
            closesocket(socket);
            socket = INVALID_SOCKET;
        }
    }
    static bool wait_socket(SOCKET socket, bool writing, ULONGLONG deadline, volatile LONG *cancel = nullptr)
    {
        for (;;)
        {
            if (cancel && InterlockedCompareExchange(cancel, 0, 0))
                return false;
            ULONGLONG now = GetTickCount64();
            if (now >= deadline)
                return false;
            ULONGLONG left = deadline - now;
            if (cancel && left > 100)
                left = 100;
            timeval timeout{static_cast<long>(left / 1000), static_cast<long>((left % 1000) * 1000)};
            fd_set selected, errors;
            FD_ZERO(&selected);
            FD_SET(socket, &selected);
            FD_ZERO(&errors);
            FD_SET(socket, &errors);
            int result =
                select(0, writing ? nullptr : &selected, writing ? &selected : nullptr, &errors, &timeout);
            if (result == 0)
                continue;
            return result > 0 && !FD_ISSET(socket, &errors) && FD_ISSET(socket, &selected);
        }
    }
    static bool address(const char *host, uint16_t port, sockaddr_in &out)
    {
        out = {};
        out.sin_family = AF_INET;
        out.sin_port = htons(port);
        return inet_pton(AF_INET, host, &out.sin_addr) == 1;
    }
    SOCKET connect_tcp(const char *host, uint16_t port, DWORD timeout_ms)
    {
        sockaddr_in peer;
        if (!port || !address(host, port, peer))
            return INVALID_SOCKET;
        SOCKET socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (socket == INVALID_SOCKET)
            return socket;
        u_long nonblocking = 1;
        if (ioctlsocket(socket, FIONBIO, &nonblocking))
        {
            close_socket(socket);
            return INVALID_SOCKET;
        }
        int result = connect(socket, reinterpret_cast<sockaddr *>(&peer), sizeof(peer));
        if (result == SOCKET_ERROR)
        {
            int error = WSAGetLastError();
            if ((error != WSAEWOULDBLOCK && error != WSAEINPROGRESS) ||
                !wait_socket(socket, true, GetTickCount64() + timeout_ms))
            {
                close_socket(socket);
                return INVALID_SOCKET;
            }
            int failure = 0;
            int length = sizeof(failure);
            if (getsockopt(socket, SOL_SOCKET, SO_ERROR, reinterpret_cast<char *>(&failure), &length) || failure)
            {
                close_socket(socket);
                return INVALID_SOCKET;
            }
        }
        // Keep sockets nonblocking so a readiness race cannot exceed the deadline.
        BOOL no_delay = TRUE;
        setsockopt(socket, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char *>(&no_delay), sizeof(no_delay));
        return socket;
    }
    SOCKET listen_tcp(const char *host, uint16_t port)
    {
        sockaddr_in local;
        if (!address(host, port, local))
            return INVALID_SOCKET;
        SOCKET socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (socket == INVALID_SOCKET)
            return socket;
        BOOL exclusive = TRUE;
        if (setsockopt(socket, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char *>(&exclusive),
                       sizeof(exclusive)) ||
            bind(socket, reinterpret_cast<sockaddr *>(&local), sizeof(local)) || listen(socket, SOMAXCONN))
        {
            close_socket(socket);
            return INVALID_SOCKET;
        }
        u_long nonblocking = 1;
        if (ioctlsocket(socket, FIONBIO, &nonblocking))
            close_socket(socket);
        return socket;
    }
    SOCKET accept_tcp(SOCKET listener, DWORD timeout_ms)
    {
        if (!wait_socket(listener, false, GetTickCount64() + timeout_ms))
            return INVALID_SOCKET;
        SOCKET socket = accept(listener, nullptr, nullptr);
        if (socket != INVALID_SOCKET)
        {
            u_long nonblocking = 1;
            if (ioctlsocket(socket, FIONBIO, &nonblocking))
                close_socket(socket);
            else
            {
                BOOL no_delay = TRUE;
                setsockopt(socket, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char *>(&no_delay),
                           sizeof(no_delay));
            }
        }
        return socket;
    }
    static bool transfer(SOCKET socket, unsigned char *bytes, size_t length, bool writing, DWORD timeout_ms,
                         volatile LONG *cancel = nullptr)
    {
        ULONGLONG deadline = GetTickCount64() + timeout_ms;
        size_t offset = 0;
        while (offset < length)
        {
            if (!wait_socket(socket, writing, deadline, cancel))
                return false;
            int remaining = static_cast<int>(length - offset);
            int count = writing ? send(socket, reinterpret_cast<const char *>(bytes + offset), remaining, 0)
                                : recv(socket, reinterpret_cast<char *>(bytes + offset), remaining, 0);
            if (count == SOCKET_ERROR && WSAGetLastError() == WSAEWOULDBLOCK)
                continue;
            if (count <= 0)
                return false;
            offset += static_cast<size_t>(count);
        }
        return true;
    }
    bool send_message(SOCKET socket, const Message &message, DWORD timeout_ms)
    {
        unsigned char wire[WireSize];
        return encode(message, wire) && transfer(socket, wire, WireSize, true, timeout_ms);
    }
    bool receive_message(SOCKET socket, Message &message, DWORD timeout_ms, volatile LONG *cancel)
    {
        unsigned char wire[WireSize];
        return transfer(socket, wire, WireSize, false, timeout_ms, cancel) && decode(wire, message);
    }
} // namespace ikp
