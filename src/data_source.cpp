#include "ikp/network.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
using namespace ikp;
static bool parse(const char *text, uint64_t &value)
{
    if (!text || !*text || *text == '-' || *text == '+')
        return false;
    for (const char *digit = text; *digit; ++digit)
        if (*digit < '0' || *digit > '9')
            return false;
    char *end = nullptr;
    errno = 0;
    unsigned long long number = strtoull(text, &end, 10);
    if (errno || *end)
        return false;
    value = number;
    return true;
}
int main(int argc, char **argv)
{
    if (argc < 7 || argc > 8)
    {
        fprintf(stderr,
                "Usage: data_source IPv4 PORT country|region|station TARGET CUTOFF COUNT [TIMEOUT_MS]\n"
                "Country target = 0. CUTOFF = Unix seconds, 0 = latest. COUNT = sequential queries.\n");
        return 2;
    }
    uint64_t port, target, cutoff, count, timeout = 10000;
    if (!parse(argv[2], port) || !port || port > 65535 || !parse(argv[4], target) || target > UINT32_MAX ||
        !parse(argv[5], cutoff) || !parse(argv[6], count) || !count || count > 10000000 ||
        (argc == 8 && (!parse(argv[7], timeout) || !timeout || timeout > 3600000)))
        return 2;
    Message query;
    query.sender_id = UINT32_MAX;
    query.target_id = static_cast<uint32_t>(target);
    query.cutoff = cutoff;
    if (!strcmp(argv[3], "country"))
        query.scope = Scope::Country;
    else if (!strcmp(argv[3], "region"))
        query.scope = Scope::Region;
    else if (!strcmp(argv[3], "station"))
        query.scope = Scope::Station;
    else
        return 2;
    query.request_id = 1;
    if (!valid_message(query))
        return 2;
    Winsock winsock;
    if (!winsock.ready())
        return 1;
    SOCKET socket = connect_tcp(argv[1], static_cast<uint16_t>(port));
    if (socket == INVALID_SOCKET)
    {
        fprintf(stderr, "Cannot connect to root.\n");
        return 1;
    }
    const ULONGLONG start = GetTickCount64();
    for (uint64_t i = 1; i <= count; ++i)
    {
        query.request_id = i;
        if (!send_message(socket, query))
        {
            fprintf(stderr, "Send failed for request %llu.\n", static_cast<unsigned long long>(i));
            close_socket(socket);
            return 1;
        }
        Message result;
        bool received = false;
        ULONGLONG deadline = GetTickCount64() + timeout;
        while (GetTickCount64() < deadline)
        {
            ULONGLONG now = GetTickCount64();
            if (now >= deadline)
                break;
            DWORD remaining = static_cast<DWORD>(deadline - now);
            if (!remaining || !receive_message(socket, result, remaining))
                break;
            if (result.type == MessageType::Update)
                continue;
            if (result.type != MessageType::Result || result.request_id != query.request_id ||
                result.scope != query.scope || result.target_id != query.target_id ||
                result.cutoff != query.cutoff)
                break;
            received = true;
            break;
        }
        if (!received)
        {
            fprintf(stderr, "Missing or invalid result for request %llu.\n",
                    static_cast<unsigned long long>(i));
            close_socket(socket);
            return 1;
        }
        double turnout = result.registered ? 100.0 * static_cast<double>(result.voted) /
                                                 static_cast<double>(result.registered)
                                           : 0.0;
        printf("request=%llu status=%u registered=%llu voted=%llu turnout=%.4f%%\n",
               static_cast<unsigned long long>(i), static_cast<unsigned>(result.status),
               static_cast<unsigned long long>(result.registered),
               static_cast<unsigned long long>(result.voted), turnout);
    }
    fprintf(stderr, "Completed %llu queries in %llu ms.\n", static_cast<unsigned long long>(count),
            static_cast<unsigned long long>(GetTickCount64() - start));
    close_socket(socket);
    return 0;
}
