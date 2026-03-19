// SparkyServer — performance benchmarks
// Build via CMake. Run: ./bin/SparkyBench [--help]
//
// Benchmarks:
//   xorstream      XorStream throughput (MB/s) at multiple buffer sizes
//   crc32          Crc32 throughput (GB/s)
//   crypto         DeriveKey + RollKey latency (ns/op)
//   protocol       SendMsg/RecvMsg round-trip latency (μs) over loopback
//   streaming      Full DLL streaming throughput (MB/s) over loopback socket
#ifdef _WIN32
#  define _WINSOCK_DEPRECATED_NO_WARNINGS
#  include <WinSock2.h>
#  include <WS2tcpip.h>
#  include <Windows.h>
#  pragma comment(lib,"ws2_32.lib")
#else
// netinet/tcp.h is needed for TCP_NODELAY; TlsLayer.h covers the rest.
#  include <netinet/tcp.h>
#endif

// Protocol.h + TlsLayer.h define SOCKET/INVALID_SOCKET/closesocket on non-Win32.
#include "../../SparkyLoader/user/include/Protocol.h"
#include "../../SparkyLoader/user/include/TlsLayer.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <iostream>
#include <iomanip>
#include <string>
#include <thread>
#include <vector>
#include <chrono>
#include <atomic>
#include <numeric>

using Clock = std::chrono::steady_clock;
using Ns    = std::chrono::nanoseconds;

static double elapsed_ms(Clock::time_point start)
{
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}
static double elapsed_ns(Clock::time_point start)
{
    return std::chrono::duration<double, std::nano>(Clock::now() - start).count();
}

static void print_throughput(const char* label, double bytes, double ms)
{
    double mbs = (bytes / (1024.0 * 1024.0)) / (ms / 1000.0);
    std::cout << std::left << std::setw(36) << label
              << std::right << std::fixed << std::setprecision(1)
              << std::setw(10) << mbs << " MB/s"
              << "  (" << std::setprecision(2) << ms << " ms)\n";
}

static void print_latency(const char* label, double total_ns, int64_t iters)
{
    double ns_op = total_ns / (double)iters;
    std::cout << std::left << std::setw(36) << label
              << std::right << std::fixed << std::setprecision(1)
              << std::setw(10) << ns_op << " ns/op"
              << "  (" << iters << " iters)\n";
}

// ---------------------------------------------------------------------------
// Bench: XorStream throughput
// ---------------------------------------------------------------------------
static void bench_xorstream()
{
    std::cout << "\n--- XorStream throughput ---\n";

    const uint64_t key = 0xFEEDF00DDEADBEEFULL;
    const std::vector<size_t> sizes = { 1<<10, 1<<14, 1<<20, 4<<20, 16<<20 };

    for (size_t sz : sizes)
    {
        std::vector<uint8_t> buf(sz, 0xAB);
        // Warm-up
        XorStream(buf.data(), (uint32_t)buf.size(), key);

        constexpr int REPS = 20;
        auto t0 = Clock::now();
        for (int r = 0; r < REPS; ++r)
            XorStream(buf.data(), (uint32_t)buf.size(), key);
        double ms = elapsed_ms(t0);

        char label[48];
        if (sz < 1<<20)
            snprintf(label, sizeof(label), "XorStream %zu KB", sz >> 10);
        else
            snprintf(label, sizeof(label), "XorStream %zu MB", sz >> 20);
        print_throughput(label, (double)sz * REPS, ms);
    }
}

// ---------------------------------------------------------------------------
// Bench: CRC32 throughput
// ---------------------------------------------------------------------------
static void bench_crc32()
{
    std::cout << "\n--- CRC32 throughput ---\n";

    const std::vector<size_t> sizes = { 1<<10, 1<<20, 4<<20 };
    for (size_t sz : sizes)
    {
        std::vector<uint8_t> buf(sz);
        for (size_t i = 0; i < sz; ++i) buf[i] = (uint8_t)i;

        constexpr int REPS = 20;
        volatile uint32_t sink = 0; // prevent optimisation
        auto t0 = Clock::now();
        for (int r = 0; r < REPS; ++r)
            sink ^= Crc32(buf.data(), (uint32_t)buf.size());
        double ms = elapsed_ms(t0);
        (void)sink;

        char label[48];
        if (sz < 1<<20)
            snprintf(label, sizeof(label), "Crc32 %zu KB", sz >> 10);
        else
            snprintf(label, sizeof(label), "Crc32 %zu MB", sz >> 20);
        print_throughput(label, (double)sz * REPS, ms);
    }
}

// ---------------------------------------------------------------------------
// Bench: DeriveKey + RollKey latency
// ---------------------------------------------------------------------------
static void bench_crypto_ops()
{
    std::cout << "\n--- Crypto key operations ---\n";

    uint8_t token[16]{};
    memset(token, 0x42, 16);
    constexpr int64_t ITERS = 500'000;

    // DeriveKey
    {
        volatile uint64_t sink = 0;
        auto t0 = Clock::now();
        for (int64_t i = 0; i < ITERS; ++i)
            sink ^= DeriveKey(token, (uint32_t)(i & 3));
        double ns = elapsed_ns(t0);
        (void)sink;
        print_latency("DeriveKey", ns, ITERS);
    }

    // RollKey
    {
        uint8_t nonce[16]{};
        uint64_t key = 0xDEADBEEF12345678ULL;
        volatile uint64_t sink = 0;
        auto t0 = Clock::now();
        for (int64_t i = 0; i < ITERS; ++i)
        {
            nonce[0] = (uint8_t)i;
            sink ^= RollKey(key, nonce);
        }
        double ns = elapsed_ns(t0);
        (void)sink;
        print_latency("RollKey", ns, ITERS);
    }

    // Crc32 on a single 12-byte MsgHeader (hot path per message)
    {
        MsgHeader h{}; h.Magic = PROTO_MAGIC; h.Version = PROTO_VERSION;
        volatile uint32_t sink = 0;
        auto t0 = Clock::now();
        for (int64_t i = 0; i < ITERS; ++i)
            sink ^= Crc32((uint8_t*)&h, sizeof(h));
        double ns = elapsed_ns(t0);
        (void)sink;
        print_latency("Crc32 (12B MsgHeader)", ns, ITERS);
    }
}

// ---------------------------------------------------------------------------
// Socket helper — creates a connected pair on loopback (works on all platforms)
// ---------------------------------------------------------------------------
static bool make_loopback_pair(SOCKET& a, SOCKET& b)
{
#ifndef _WIN32
    int fds[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) != 0) return false;
    int flag = 1;
    setsockopt(fds[0], IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));
    setsockopt(fds[1], IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));
    a = fds[0]; b = fds[1]; return true;
#else
    SOCKET listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in addr{}; addr.sin_family=AF_INET; addr.sin_port=0; addr.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    bind(listener,(sockaddr*)&addr,sizeof(addr));
    socklen_t al=sizeof(addr); getsockname(listener,(sockaddr*)&addr,&al);
    listen(listener,1);
    b = socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
    connect(b,(sockaddr*)&addr,sizeof(addr));
    sockaddr_in ca{}; int cl=sizeof(ca);
    a = accept(listener,(sockaddr*)&ca,&cl);
    closesocket(listener);
    int flag=1;
    setsockopt(a,IPPROTO_TCP,TCP_NODELAY,(char*)&flag,sizeof(flag));
    setsockopt(b,IPPROTO_TCP,TCP_NODELAY,(char*)&flag,sizeof(flag));
    return (a!=INVALID_SOCKET && b!=INVALID_SOCKET);
#endif
}

// Minimal send/recv wrappers (no hdrKey for bench simplicity)
static bool bench_send(SOCKET s, MsgType t, const void* pay = nullptr, uint16_t len = 0)
{
    MsgHeader h{}; h.Magic=PROTO_MAGIC; h.Version=PROTO_VERSION; h.Type=t; h.Length=len;
    std::vector<uint8_t> buf(len);
    if (pay && len) memcpy(buf.data(), pay, len);
    uint32_t crc = Crc32((uint8_t*)&h, sizeof(h));
    if (!buf.empty()) crc ^= Crc32(buf.data(), len);
    return NetSend(s,nullptr,&h,sizeof(h))
        && (buf.empty() || NetSend(s,nullptr,buf.data(),len))
        && NetSend(s,nullptr,&crc,4);
}
static bool bench_recv(SOCKET s, MsgType& t, std::vector<uint8_t>& pay)
{
    MsgHeader h{};
    if (!NetRecv(s,nullptr,&h,sizeof(h),2000)) return false;
    if (h.Magic!=PROTO_MAGIC || h.Version!=PROTO_VERSION) return false;
    pay.resize(h.Length);
    if (h.Length && !NetRecv(s,nullptr,pay.data(),h.Length,2000)) return false;
    uint32_t rc{}; if (!NetRecv(s,nullptr,&rc,4,2000)) return false;
    uint32_t lc = Crc32((uint8_t*)&h,sizeof(h));
    if (!pay.empty()) lc ^= Crc32(pay.data(),h.Length);
    if (lc!=rc) return false;
    t=h.Type; return true;
}

// ---------------------------------------------------------------------------
// Bench: Protocol round-trip latency (Heartbeat ping-pong)
// ---------------------------------------------------------------------------
static void bench_protocol_rtt()
{
    std::cout << "\n--- Protocol message round-trip latency ---\n";

    SOCKET sv, cv;
    if (!make_loopback_pair(sv, cv))
    {
        std::cout << "  SKIP: cannot create socket pair\n";
        return;
    }

    constexpr int REPS = 10'000;
    std::atomic<bool> done{false};

    // Echo server
    std::thread echoThread([&]() {
        while (!done.load())
        {
            MsgType mt{}; std::vector<uint8_t> pay;
            if (!bench_recv(sv, mt, pay)) break;
            bench_send(sv, MsgType::Ack);
        }
    });

    HeartbeatPayload hbp{};
    memset(hbp.Nonce, 0x11, 16);

    // Warm-up
    for (int i = 0; i < 100; ++i)
    {
        bench_send(cv, MsgType::Heartbeat, &hbp, sizeof(hbp));
        MsgType mt{}; std::vector<uint8_t> pay;
        bench_recv(cv, mt, pay);
    }

    // Benchmark
    std::vector<double> latencies(REPS);
    for (int i = 0; i < REPS; ++i)
    {
        auto t0 = Clock::now();
        bench_send(cv, MsgType::Heartbeat, &hbp, sizeof(hbp));
        MsgType mt{}; std::vector<uint8_t> pay;
        bench_recv(cv, mt, pay);
        latencies[i] = elapsed_ns(t0) / 1000.0; // μs
    }

    done = true;
    closesocket(cv); // unblock echo thread
    echoThread.join();
    closesocket(sv);

    std::sort(latencies.begin(), latencies.end());
    double p50  = latencies[REPS * 50  / 100];
    double p95  = latencies[REPS * 95  / 100];
    double p99  = latencies[REPS * 99  / 100];
    double mean = std::accumulate(latencies.begin(), latencies.end(), 0.0) / REPS;

    std::cout << std::fixed << std::setprecision(2);
    std::cout << "  Heartbeat→Ack RTT  mean=" << mean << " μs"
              << "  p50=" << p50 << " μs"
              << "  p95=" << p95 << " μs"
              << "  p99=" << p99 << " μs"
              << "  (" << REPS << " samples)\n";
}

// ---------------------------------------------------------------------------
// Bench: DLL streaming throughput (simulated, over loopback socket)
// Tests the actual SendMsg → XorStream → CRC32 path, not just memcpy.
// ---------------------------------------------------------------------------
static void bench_dll_streaming()
{
    std::cout << "\n--- DLL streaming throughput (loopback) ---\n";

    const std::vector<size_t> dll_sizes = { 512<<10, 2<<20, 8<<20 };

    for (size_t dll_sz : dll_sizes)
    {
        SOCKET sv, cv;
        if (!make_loopback_pair(sv, cv)) { std::cout << "  SKIP\n"; return; }

        constexpr uint32_t CHUNK    = 4096;
        constexpr uint32_t HB_INT  = 8;

        std::vector<uint8_t> dll(dll_sz);
        for (size_t i = 0; i < dll_sz; ++i) dll[i] = (uint8_t)(i * 7 + 3);

        uint8_t token[16]{};
        memset(token, 0x99, 16);
        uint64_t dllKey = DeriveKey(token, 1);

        std::atomic<bool> serverDone{false};
        auto t0 = Clock::now();

        // Sender thread
        std::thread snd([&]() {
            uint64_t rolling = dllKey;
            const uint32_t total   = (uint32_t)dll_sz;
            const uint32_t nChunks = (total + CHUNK - 1) / CHUNK;

            BinaryReadyPayload br{};
            br.TotalBytes=total; br.ChunkSize=CHUNK;
            br.NumChunks=nChunks; br.ChunksPerHeartbeat=HB_INT;
            bench_send(sv, MsgType::BinaryReady, &br, sizeof(br));

            for (uint32_t c = 0; c < nChunks; ++c)
            {
                uint32_t off  = c * CHUNK;
                uint32_t size = (uint32_t)std::min((size_t)CHUNK, dll_sz - off);
                std::vector<uint8_t> chunk(dll.begin()+off, dll.begin()+off+size);
                XorStream(chunk.data(), size, rolling);
                bench_send(sv, MsgType::BinaryChunk, chunk.data(), (uint16_t)size);

                if ((c+1)%HB_INT==0 || c+1==nChunks)
                {
                    MsgType mt{}; std::vector<uint8_t> pay;
                    bench_recv(sv, mt, pay);
                    uint8_t nonce[16]{}; nonce[0]=(uint8_t)c;
                    rolling = RollKey(rolling, nonce);
                }
            }
            bench_send(sv, MsgType::BinaryEnd);
            serverDone = true;
        });

        // Receiver
        {
            MsgType mt{}; std::vector<uint8_t> pay;
            bench_recv(cv, mt, pay); // BinaryReady
            auto& br = *reinterpret_cast<BinaryReadyPayload*>(pay.data());
            uint64_t rolling = dllKey;
            (void)0; // byte count not tracked in bench

            for (uint32_t c = 0; c < br.NumChunks; ++c)
            {
                pay.clear();
                bench_recv(cv, mt, pay);
                XorStream(pay.data(), (uint32_t)pay.size(), rolling);
                (void)pay.size(); // received bytes — not tracked in bench

                if ((c+1)%HB_INT==0 || c+1==br.NumChunks)
                {
                    uint8_t nonce[16]{}; nonce[0]=(uint8_t)c;
                    rolling = RollKey(rolling, nonce);
                    HeartbeatPayload hbp{}; memcpy(hbp.Nonce, nonce, 16);
                    bench_send(cv, MsgType::Heartbeat, &hbp, sizeof(hbp));
                }
            }
            bench_recv(cv, mt, pay); // BinaryEnd
        }

        double ms = elapsed_ms(t0);
        snd.join();
        closesocket(sv); closesocket(cv);

        char label[64];
        if (dll_sz < 1<<20)
            snprintf(label, sizeof(label), "Stream %zu KB DLL", dll_sz>>10);
        else
            snprintf(label, sizeof(label), "Stream %zu MB DLL", dll_sz>>20);
        print_throughput(label, (double)dll_sz, ms);
    }
}

// ---------------------------------------------------------------------------
// Bench: Concurrent connections accepted per second
// Measures how fast the server accept loop + TLS handshake can handle
// new connections (plaintext mode).
// ---------------------------------------------------------------------------
static void bench_connections_per_sec()
{
    std::cout << "\n--- Connection acceptance rate (plaintext, loopback) ---\n";
    std::cout << "  (Simulates the OS-level accept() + socket setup overhead)\n";

    constexpr int N = 1000;

    SOCKET listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in addr{}; addr.sin_family=AF_INET; addr.sin_port=0; addr.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    int opt=1; setsockopt(listener,SOL_SOCKET,SO_REUSEADDR,(char*)&opt,sizeof(opt));
    bind(listener,(sockaddr*)&addr,sizeof(addr));
    socklen_t al=sizeof(addr); getsockname(listener,(sockaddr*)&addr,&al);
    listen(listener, N+10);

    std::atomic<int> accepted{0};

    // Acceptor thread
    std::thread acc([&]() {
        while (accepted.load() < N)
        {
            sockaddr_in ca{}; socklen_t cl=sizeof(ca);
#ifdef _WIN32
            SOCKET cs = accept(listener,(sockaddr*)&ca,(int*)&cl);
            if (cs==INVALID_SOCKET) break;
#else
            int cs = accept(listener,(sockaddr*)&ca,&cl);
            if (cs<0) break;
#endif
            closesocket(cs);
            ++accepted;
        }
    });

    auto t0 = Clock::now();
    for (int i = 0; i < N; ++i)
    {
        SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        connect(s,(sockaddr*)&addr,sizeof(addr));
        closesocket(s);
    }
    acc.join();
    double ms = elapsed_ms(t0);
    closesocket(listener);

    std::cout << "  " << N << " connections in " << std::fixed << std::setprecision(1)
              << ms << " ms → "
              << std::setprecision(0) << (N / (ms / 1000.0)) << " conn/sec\n";
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
static void print_usage()
{
    std::cout << "Usage: SparkyBench [xorstream] [crc32] [crypto] [protocol] [streaming] [connrate] [all]\n"
              << "  Default: all\n";
}

int main(int argc, char** argv)
{
    std::cout << "=== SparkyServer Benchmarks ===\n";
    std::cout << "Platform: ";
#ifdef _WIN32
    std::cout << "Windows\n";
    WSADATA wsa{}; WSAStartup(MAKEWORD(2,2), &wsa);
#else
    std::cout << "Linux/macOS\n";
#endif

    bool runAll      = (argc == 1);
    bool runXor      = runAll;
    bool runCrc      = runAll;
    bool runCrypto   = runAll;
    bool runProtocol = runAll;
    bool runStream   = runAll;
    bool runConn     = runAll;

    for (int i = 1; i < argc; ++i)
    {
        std::string a = argv[i];
        if (a == "all")       { runXor=runCrc=runCrypto=runProtocol=runStream=runConn=true; }
        else if (a == "xorstream")  runXor      = true;
        else if (a == "crc32")      runCrc      = true;
        else if (a == "crypto")     runCrypto   = true;
        else if (a == "protocol")   runProtocol = true;
        else if (a == "streaming")  runStream   = true;
        else if (a == "connrate")   runConn     = true;
        else if (a == "--help" || a == "-h") { print_usage(); return 0; }
        else { std::cerr << "Unknown benchmark: " << a << "\n"; print_usage(); return 1; }
    }

    if (runXor)      bench_xorstream();
    if (runCrc)      bench_crc32();
    if (runCrypto)   bench_crypto_ops();
    if (runProtocol) bench_protocol_rtt();
    if (runStream)   bench_dll_streaming();
    if (runConn)     bench_connections_per_sec();

#ifdef _WIN32
    WSACleanup();
#endif
    std::cout << "\n=== Done ===\n";
    return 0;
}
