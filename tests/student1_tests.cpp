#include "ikp/routing.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
using namespace ikp;
#define CHECK(expression)                                                                                    \
    do                                                                                                       \
    {                                                                                                        \
        if (!(expression))                                                                                   \
        {                                                                                                    \
            fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #expression);                                    \
            exit(1);                                                                                         \
        }                                                                                                    \
    } while (0)

// Test adapter for Student 2's circular buffer. Production buffer belongs to Student 2.
class TestSink : public InboundSink
{
    CRITICAL_SECTION lock_;
    CONDITION_VARIABLE condition_;
    ReceivedMessage slot_;
    bool full_ = false, closed_ = false;

  public:
    TestSink()
    {
        InitializeCriticalSection(&lock_);
        InitializeConditionVariable(&condition_);
    }
    ~TestSink()
    {
        DeleteCriticalSection(&lock_);
    }
    bool push(const ReceivedMessage &item) override
    {
        EnterCriticalSection(&lock_);
        while (full_ && !closed_)
            SleepConditionVariableCS(&condition_, &lock_, INFINITE);
        if (closed_)
        {
            LeaveCriticalSection(&lock_);
            return false;
        }
        slot_ = item;
        full_ = true;
        WakeAllConditionVariable(&condition_);
        LeaveCriticalSection(&lock_);
        return true;
    }
    bool pop(ReceivedMessage &item)
    {
        EnterCriticalSection(&lock_);
        ULONGLONG deadline = GetTickCount64() + 5000;
        while (!full_ && !closed_)
        {
            ULONGLONG now = GetTickCount64();
            if (now >= deadline)
            {
                LeaveCriticalSection(&lock_);
                return false;
            }
            SleepConditionVariableCS(&condition_, &lock_, static_cast<DWORD>(deadline - now));
        }
        if (!full_)
        {
            LeaveCriticalSection(&lock_);
            return false;
        }
        item = slot_;
        full_ = false;
        WakeAllConditionVariable(&condition_);
        LeaveCriticalSection(&lock_);
        return true;
    }
    void close() override
    {
        EnterCriticalSection(&lock_);
        closed_ = true;
        WakeAllConditionVariable(&condition_);
        LeaveCriticalSection(&lock_);
    }
};
static Message query(uint64_t id = 1)
{
    Message m;
    m.request_id = id;
    m.sender_id = UINT32_MAX;
    return m;
}
static uint16_t port_of(SOCKET socket)
{
    sockaddr_in address{};
    int length = sizeof(address);
    CHECK(getsockname(socket, reinterpret_cast<sockaddr *>(&address), &length) == 0);
    return ntohs(address.sin_port);
}
static void pair(SOCKET &client, SOCKET &server)
{
    SOCKET listener = listen_tcp("127.0.0.1", 0);
    CHECK(listener != INVALID_SOCKET);
    client = connect_tcp("127.0.0.1", port_of(listener));
    CHECK(client != INVALID_SOCKET);
    server = accept_tcp(listener);
    CHECK(server != INVALID_SOCKET);
    close_socket(listener);
}
static void protocol_tests()
{
    Message original = query(UINT64_MAX);
    original.cutoff = UINT64_C(0x0102030405060708);
    unsigned char wire[WireSize];
    CHECK(encode(original, wire));
    CHECK(wire[0] == 'I' && wire[1] == 'K' && wire[32] == 1 && wire[39] == 8);
    Message decoded;
    CHECK(decode(wire, decoded));
    CHECK(decoded.request_id == UINT64_MAX && decoded.cutoff == original.cutoff);
    wire[4] = 1;
    CHECK(!decode(wire, decoded));
    original.type = MessageType::Result;
    original.registered = UINT64_MAX;
    original.voted = UINT64_MAX - 1;
    CHECK(encode(original, wire) && decode(wire, decoded));
    CHECK(decoded.voted == UINT64_MAX - 1);
    original.voted = 5;
    original.registered = 4;
    CHECK(!encode(original, wire));
    SOCKET client, server;
    pair(client, server);
    original = query();
    CHECK(encode(original, wire));
    // TCP segmentation and multiple frames in a single byte stream.
    CHECK(send(client, reinterpret_cast<const char *>(wire), 3, 0) == 3);
    CHECK(send(client, reinterpret_cast<const char *>(wire + 3), WireSize - 3, 0) == WireSize - 3);
    CHECK(send_message(client, original));
    CHECK(receive_message(server, decoded) && receive_message(server, decoded));
    ULONGLONG start = GetTickCount64();
    CHECK(!receive_message(server, decoded, 30));
    CHECK(GetTickCount64() - start < 1000);
    CHECK(send(client, reinterpret_cast<const char *>(wire), 7, 0) == 7);
    close_socket(client);
    CHECK(!receive_message(server, decoded));
    close_socket(server);
    puts("PASS protocol, TCP framing, truncation, deadlines");
}
static void map_tests()
{
    HashMap<uint64_t> map;
    for (uint64_t i = 0; i < 10000; ++i)
        CHECK(map.insert(i, i * 3));
    CHECK(!map.insert(1, 5));
    for (uint64_t i = 0; i < 10000; ++i)
        CHECK(map.find(i) && *map.find(i) == i * 3);
    for (uint64_t i = 0; i < 10000; i += 2)
        CHECK(map.erase(i));
    CHECK(map.size() == 5000 && !map.erase(10001));
    map.clear();
    CHECK(map.size() == 0);
    CHECK(map.insert(UINT64_MAX, 9));
    puts("PASS HashMap 10000 entries, rehash, erase, reuse");
}
struct Producer
{
    OutboundQueue *queue;
    uint64_t first;
    unsigned count;
    bool result = true;
};
static DWORD WINAPI produce(void *pointer)
{
    Producer &producer = *static_cast<Producer *>(pointer);
    for (unsigned i = 0; i < producer.count; ++i)
        if (!producer.queue->push(OutboundMessage{INVALID_SOCKET, query(producer.first + i)}))
        {
            producer.result = false;
            break;
        }
    return 0;
}
struct Consumer
{
    OutboundQueue *queue;
    bool result = true;
};
static DWORD WINAPI consume_one(void *pointer)
{
    Consumer &consumer = *static_cast<Consumer *>(pointer);
    OutboundMessage message;
    consumer.result = consumer.queue->pop(message);
    return 0;
}
static void queue_tests()
{
    OutboundQueue queue(7);
    Producer producers[4];
    HANDLE handles[4];
    for (unsigned i = 0; i < 4; ++i)
    {
        producers[i] = Producer{&queue, 1 + i * 2500, 2500};
        handles[i] = CreateThread(nullptr, 0, produce, &producers[i], 0, nullptr);
        CHECK(handles[i]);
    }
    bool seen[10001] = {};
    uint64_t last[4] = {};
    for (unsigned i = 0; i < 10000; ++i)
    {
        OutboundMessage item;
        CHECK(queue.pop(item));
        uint64_t id = item.message.request_id;
        CHECK(id && id <= 10000 && !seen[id]);
        seen[id] = true;
        size_t producer = static_cast<size_t>((id - 1) / 2500);
        CHECK(id > last[producer]);
        last[producer] = id;
    }
    CHECK(WaitForMultipleObjects(4, handles, TRUE, 5000) == WAIT_OBJECT_0);
    for (unsigned i = 0; i < 4; ++i)
    {
        CHECK(producers[i].result);
        CloseHandle(handles[i]);
    }
    queue.close();
    OutboundMessage item;
    CHECK(!queue.pop(item));
    CHECK(!queue.push(item));
    OutboundQueue full(1);
    CHECK(full.push(OutboundMessage{}));
    Producer blocked{&full, 1, 1};
    HANDLE thread = CreateThread(nullptr, 0, produce, &blocked, 0, nullptr);
    CHECK(thread);
    Sleep(20);
    full.close();
    CHECK(WaitForSingleObject(thread, 3000) == WAIT_OBJECT_0);
    CHECK(!blocked.result);
    CloseHandle(thread);
    CHECK(full.pop(item) && !full.pop(item));
    OutboundQueue empty(1);
    Consumer consumer{&empty};
    HANDLE waiting = CreateThread(nullptr, 0, consume_one, &consumer, 0, nullptr);
    CHECK(waiting);
    Sleep(20);
    empty.close();
    CHECK(WaitForSingleObject(waiting, 3000) == WAIT_OBJECT_0);
    CHECK(!consumer.result);
    CloseHandle(waiting);
    puts("PASS outbound FIFO 4 producers / 10000 messages, backpressure, shutdown");
}
static bool contains(const RoutePlan &plan, uint32_t id)
{
    for (size_t i = 0; i < plan.count(); ++i)
        if (plan.child_id(i) == id)
            return true;
    return false;
}
static void topology_tests(const char *path)
{
    Topology topology;
    char error[256];
    CHECK(topology.load(path, error, sizeof(error)));
    CHECK(topology.root_id() == 1 && topology.size() == 7);
    RoutePlan plan;
    Message m = query();
    CHECK(plan.build(topology, 1, m) && plan.count() == 2 && contains(plan, 10) && contains(plan, 20));
    m.scope = Scope::Region;
    m.target_id = 11;
    CHECK(plan.build(topology, 1, m) && plan.count() == 1 && contains(plan, 10));
    CHECK(plan.build(topology, 10, m) && plan.count() == 1 && contains(plan, 11));
    m.target_id = 10;
    CHECK(plan.build(topology, 10, m) && plan.count() == 2); // Includes nested region 11.
    CHECK(plan.build(topology, 11, m) && plan.count() == 1 && contains(plan, 101));
    m.scope = Scope::Station;
    m.target_id = 201;
    CHECK(plan.build(topology, 1, m) && plan.count() == 1 && contains(plan, 20));
    CHECK(plan.build(topology, 10, m) && plan.count() == 0);
    m.target_id = 20;
    CHECK(plan.build(topology, 1, m) && plan.count() == 0);
    m.scope = Scope::Region;
    m.target_id = 999;
    CHECK(plan.build(topology, 1, m) && plan.count() == 0);
    puts("PASS country, nested regions, station and unknown-target routing");
}
static void temp_path(char (&path)[MAX_PATH])
{
    char directory[MAX_PATH];
    CHECK(GetTempPathA(MAX_PATH, directory));
    CHECK(GetTempFileNameA(directory, "ikp", 0, path));
}
static void invalid_topology_tests()
{
    const char *invalid[] = {"1 0 0 agr 127.0.0.1 1\n1 0 0 agr 127.0.0.1 2\n",
                             "1 0 0 agr 127.0.0.1 1\n2 3 1 agr 127.0.0.1 2\n3 2 1 agr 127.0.0.1 3\n",
                             "1 0 0 agr 127.0.0.1 1\n2 99 1 station 127.0.0.1 2\n",
                             "1 0 0 agr 127.0.0.1 1\n2 0 0 agr 127.0.0.1 2\n",
                             "1 0 0 station 127.0.0.1 1\n",
                             "1 0 0 agr 127.0.0.1 1\n2 1 1 station 127.0.0.1 1\n",
                             "1 0 0 agr 127.0.0.1 65536\n",
                             "4294967295 0 0 agr 127.0.0.1 1\n"};
    char path[MAX_PATH];
    temp_path(path);
    for (const char *content : invalid)
    {
        FILE *file = fopen(path, "w");
        CHECK(file);
        fputs(content, file);
        fclose(file);
        Topology topology;
        char error[256];
        CHECK(!topology.load(path, error, sizeof(error)));
        CHECK(topology.size() == 0 && *error);
    }
    CHECK(DeleteFileA(path));
    puts("PASS invalid topology validation");
}
static void network_tree_test(unsigned queries)
{
    SOCKET root_listener = listen_tcp("127.0.0.1", 0);
    SOCKET middle_listener = listen_tcp("127.0.0.1", 0);
    SOCKET leaf_listener = listen_tcp("127.0.0.1", 0);
    CHECK(root_listener != INVALID_SOCKET && middle_listener != INVALID_SOCKET &&
          leaf_listener != INVALID_SOCKET);
    char path[MAX_PATH];
    temp_path(path);
    FILE *file = fopen(path, "w");
    CHECK(file);
    fprintf(file, "1 0 0 agr 127.0.0.1 %u\n10 1 10 agr 127.0.0.1 %u\n101 10 10 station 127.0.0.1 %u\n",
            port_of(root_listener), port_of(middle_listener), port_of(leaf_listener));
    fclose(file);
    Topology topology;
    char error[256];
    CHECK(topology.load(path, error, sizeof(error)));
    CHECK(DeleteFileA(path));
    TestSink root_inbound, middle_inbound;
    Transport root(topology, 1, root_inbound, 8), middle(topology, 10, middle_inbound, 8);
    CHECK(middle.connect_children());
    SOCKET leaf = accept_tcp(leaf_listener);
    CHECK(leaf != INVALID_SOCKET);
    CHECK(root.connect_children());
    SOCKET middle_parent = accept_tcp(middle_listener);
    CHECK(middle_parent != INVALID_SOCKET);
    SOCKET source = connect_tcp("127.0.0.1", port_of(root_listener));
    CHECK(source != INVALID_SOCKET);
    SOCKET root_parent = accept_tcp(root_listener);
    CHECK(root_parent != INVALID_SOCKET);
    CHECK(middle.start(middle_parent) && root.start(root_parent));
    close_socket(root_listener);
    close_socket(middle_listener);
    close_socket(leaf_listener);
    ULONGLONG start = GetTickCount64();
    for (unsigned i = 1; i <= queries; ++i)
    {
        Message request = query(i);
        request.cutoff = 1700000000;
        if (i % 3 == 1)
        {
            request.scope = Scope::Region;
            request.target_id = 10;
        }
        if (i % 3 == 2)
        {
            request.scope = Scope::Station;
            request.target_id = 101;
        }
        CHECK(send_message(source, request));
        ReceivedMessage incoming;
        CHECK(root_inbound.pop(incoming));
        CHECK(incoming.peer_id == UINT32_MAX && incoming.origin == Origin::Parent);
        RoutePlan root_plan;
        CHECK(root_plan.build(topology, 1, incoming.message) && root_plan.count() == 1);
        // Production TOZ/pool registers active request BEFORE this dispatch.
        CHECK(root_plan.dispatch(root, incoming.message) == 1);
        CHECK(middle_inbound.pop(incoming) && incoming.peer_id == 1);
        RoutePlan middle_plan;
        CHECK(middle_plan.build(topology, 10, incoming.message) && middle_plan.count() == 1);
        CHECK(middle_plan.dispatch(middle, incoming.message) == 1);
        Message leaf_query;
        CHECK(receive_message(leaf, leaf_query));
        CHECK(leaf_query.sender_id == 10 && leaf_query.request_id == i);
        // Controlled peer replaces Student 2's station and aggregation in this test.
        Message result = leaf_query;
        result.type = MessageType::Result;
        result.sender_id = 101;
        result.registered = 1000;
        result.voted = i % 1001;
        CHECK(send_message(leaf, result));
        Message child_result;
        CHECK(receive_message(middle.child(101)->socket, child_result));
        CHECK(middle.enqueue_parent(child_result));
        CHECK(receive_message(root.child(10)->socket, child_result));
        CHECK(child_result.sender_id == 10);
        CHECK(root.enqueue_parent(child_result));
        Message final;
        CHECK(receive_message(source, final));
        CHECK(final.sender_id == 1 && final.request_id == i && final.cutoff == request.cutoff &&
              final.scope == request.scope && final.target_id == request.target_id &&
              final.registered == 1000 && final.voted == i % 1001);
    }
    // Unsolicited updates travel through TS unchanged except for sender identity.
    Message update;
    update.type = MessageType::Update;
    update.sender_id = 101;
    update.registered = 1000;
    update.voted = 800;
    CHECK(middle.enqueue_parent(update));
    Message received;
    CHECK(receive_message(root.child(10)->socket, received) && received.type == MessageType::Update);
    CHECK(root.enqueue_parent(received));
    CHECK(receive_message(source, received) && received.voted == 800);
    CHECK(root.failures() == 0 && middle.failures() == 0);
    // TP may be blocked in inbound.push: shutdown must release that wait.
    CHECK(send_message(source, query(queries + 1)));
    CHECK(send_message(source, query(queries + 2)));
    Sleep(20);
    root.stop();
    middle.stop();
    close_socket(source);
    close_socket(leaf);
    printf("PASS recursive TCP TP/routing/TS %u queries + update + blocked-receiver shutdown in %llu ms\n",
           queries, static_cast<unsigned long long>(GetTickCount64() - start));
}
static void source_test(const char *executable, bool invalid_result = false)
{
    SOCKET listener = listen_tcp("127.0.0.1", 0);
    CHECK(listener != INVALID_SOCKET);
    char command[32768];
    CHECK(snprintf(command, sizeof(command), "\"%s\" 127.0.0.1 %u country 0 1700000000 3", executable,
                   port_of(listener)) > 0);
    SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    HANDLE output_read, output_write;
    CHECK(CreatePipe(&output_read, &output_write, &security, 0));
    CHECK(SetHandleInformation(output_read, HANDLE_FLAG_INHERIT, 0));
    STARTUPINFOA startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdOutput = output_write;
    startup.hStdError = output_write;
    startup.hStdInput = nullptr;
    PROCESS_INFORMATION process{};
    CHECK(CreateProcessA(nullptr, command, nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr,
                         &startup, &process));
    CloseHandle(output_write);
    SOCKET client = accept_tcp(listener, 5000);
    CHECK(client != INVALID_SOCKET);
    for (unsigned i = 1; i <= 3; ++i)
    {
        Message m;
        CHECK(receive_message(client, m));
        CHECK(m.type == MessageType::Query && m.sender_id == UINT32_MAX && m.request_id == i &&
              m.cutoff == 1700000000);
        Message update;
        update.type = MessageType::Update;
        update.sender_id = 1;
        CHECK(send_message(client, update));
        m.type = MessageType::Result;
        m.sender_id = 1;
        m.registered = 1000;
        m.voted = 375;
        if (i == 3)
            m.status = Status::Partial;
        if (invalid_result)
            ++m.request_id;
        CHECK(send_message(client, m));
        if (invalid_result)
            break;
    }
    CHECK(WaitForSingleObject(process.hProcess, 5000) == WAIT_OBJECT_0);
    DWORD code;
    CHECK(GetExitCodeProcess(process.hProcess, &code) && code == (invalid_result ? 1u : 0u));
    char output[4096] = {};
    DWORD bytes = 0;
    CHECK(ReadFile(output_read, output, sizeof(output) - 1, &bytes, nullptr));
    CloseHandle(output_read);
    if (invalid_result)
        CHECK(strstr(output, "Missing or invalid result"));
    else
    {
        CHECK(strstr(output, "turnout=37.5000%"));
        CHECK(strstr(output, "request=3 status=1"));
        CHECK(strstr(output, "Completed 3 queries"));
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    close_socket(client);
    close_socket(listener);
    puts(invalid_result
             ? "PASS Data Source rejects mismatched results"
             : "PASS Data Source executable, turnout display, partial status, repeated requests and updates");
}
struct Bootstrap
{
    Transport *transport;
    bool result = false;
};
static DWORD WINAPI bootstrap(void *context)
{
    Bootstrap &task = *static_cast<Bootstrap *>(context);
    task.result = task.transport->accept_and_start(5000);
    return 0;
}
static void transport_failure_tests()
{
    SOCKET reservation = listen_tcp("127.0.0.1", 0);
    CHECK(reservation != INVALID_SOCKET);
    char path[MAX_PATH];
    temp_path(path);
    FILE *file = fopen(path, "w");
    CHECK(file);
    fprintf(file, "1 0 0 agr 127.0.0.1 %u\n", port_of(reservation));
    fclose(file);
    close_socket(reservation);
    Topology topology;
    char error[256];
    CHECK(topology.load(path, error, sizeof(error)));
    CHECK(DeleteFileA(path));
    TestSink sink;
    Transport transport(topology, 1, sink);
    SOCKET source, parent;
    pair(source, parent);
    CHECK(transport.connect_children() && transport.start(parent));
    Message wrong = query();
    wrong.sender_id = 123;
    CHECK(send_message(source, wrong));
    ULONGLONG deadline = GetTickCount64() + 3000;
    while (!transport.parent_closed() && GetTickCount64() < deadline)
        Sleep(1);
    CHECK(transport.parent_closed());
    shutdown(parent, SD_SEND); // Force the TS failure path using its owned socket.
    Message result = query();
    result.type = MessageType::Result;
    result.sender_id = 1;
    CHECK(transport.enqueue_parent(result));
    deadline = GetTickCount64() + 3000;
    while (!transport.failures() && GetTickCount64() < deadline)
        Sleep(1);
    CHECK(transport.failures() == 1);
    transport.stop();
    close_socket(source);
    TestSink next_sink;
    Transport next(topology, 1, next_sink);
    pair(source, parent);
    CHECK(next.connect_children() && next.start(parent));
    unsigned char wire[WireSize];
    CHECK(encode(query(), wire));
    CHECK(send(source, reinterpret_cast<char *>(wire), 4, 0) == 4);
    Sleep(20);
    ULONGLONG start = GetTickCount64();
    next.stop();
    CHECK(GetTickCount64() - start < 1500);
    close_socket(source);
    CHECK(!next.start(INVALID_SOCKET) && !next.connect_children());
    TestSink boot_sink;
    Transport boot_transport(topology, 1, boot_sink);
    CHECK(boot_transport.connect_children());
    Bootstrap task{&boot_transport};
    HANDLE thread = CreateThread(nullptr, 0, bootstrap, &task, 0, nullptr);
    CHECK(thread);
    source = INVALID_SOCKET;
    for (unsigned i = 0; i < 100 && source == INVALID_SOCKET; ++i)
    {
        source = connect_tcp("127.0.0.1", topology.find(1)->port, 100);
        if (source == INVALID_SOCKET)
            Sleep(10);
    }
    CHECK(source != INVALID_SOCKET && WaitForSingleObject(thread, 6000) == WAIT_OBJECT_0 && task.result);
    CloseHandle(thread);
    CHECK(send_message(source, query()));
    ReceivedMessage incoming;
    CHECK(boot_sink.pop(incoming));
    CHECK(incoming.message.request_id == 1);
    boot_transport.stop();
    close_socket(source);
    puts("PASS parent identity, TS failure, partial-frame shutdown, bootstrap and single-session lifecycle");
}
int main(int argc, char **argv)
{
    CHECK(argc == 3);
    Winsock winsock;
    CHECK(winsock.ready());
    map_tests();
    protocol_tests();
    queue_tests();
    topology_tests(argv[1]);
    invalid_topology_tests();
    transport_failure_tests();
    network_tree_test(10000);
    source_test(argv[2]);
    source_test(argv[2], true);
    DWORD before, after;
    CHECK(GetProcessHandleCount(GetCurrentProcess(), &before));
    for (unsigned i = 0; i < 5; ++i)
        network_tree_test(10);
    CHECK(GetProcessHandleCount(GetCurrentProcess(), &after));
    CHECK(after <= before);
    printf("PASS repeated lifecycle handle count before=%lu after=%lu\n", before, after);
    puts("All Student 1 tests passed.");
    return 0;
}
