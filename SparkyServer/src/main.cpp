// SparkyServer — auth + paid check + encrypted DLL streaming
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <WinSock2.h>
#include <WS2tcpip.h>
#include <Windows.h>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <unordered_set>
#include <fstream>
#include <iostream>
#include <format>
#include <iterator>

#pragma comment(lib, "ws2_32.lib")

#include "../../SparkyLoader/include/Protocol.h"

// ---------------------------------------------------------------------------
// Config
// ---------------------------------------------------------------------------
static constexpr uint16_t LISTEN_PORT   = 7777;
static constexpr uint32_t CURRENT_BUILD = 0x0001'0000;
static constexpr uint32_t CHUNK_SIZE    = 4096;

// Files on server filesystem (relative to CWD when server is launched)
static constexpr const char* DLL_FILE      = "SparkyCore.dll";     // plain DLL (encrypted below)
static constexpr const char* CONFIG_FILE   = "config.bin";
static constexpr const char* PAID_FILE     = "paid_hwids.txt";     // one HWID hash (hex) per line
static constexpr const char* WHITELIST_FILE= "hwid_whitelist.txt"; // optional extra whitelist

// ---------------------------------------------------------------------------
// Globals
// ---------------------------------------------------------------------------
static std::unordered_set<std::string> g_paid;
static std::unordered_set<std::string> g_whitelist;
static std::mutex g_setMutex;

static std::vector<uint8_t> g_dllBytes; // cached DLL bytes (plain, loaded once)

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
static std::string HexStr(const uint8_t* d, size_t n)
{
    static const char h[] = "0123456789abcdef";
    std::string out; out.reserve(n * 2);
    for (size_t i = 0; i < n; ++i) { out += h[d[i]>>4]; out += h[d[i]&0xF]; }
    return out;
}

static std::vector<uint8_t> ReadFileFull(const char* path)
{
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f.is_open()) return {};
    std::vector<uint8_t> buf(f.tellg()); f.seekg(0);
    f.read((char*)buf.data(), buf.size());
    return buf;
}

static void LoadSet(const char* path, std::unordered_set<std::string>& set)
{
    std::ifstream f(path);
    if (!f.is_open()) return;
    std::string ln;
    std::lock_guard lk(g_setMutex);
    while (std::getline(f, ln))
        if (!ln.empty()) set.insert(ln);
}

// ---------------------------------------------------------------------------
// Per-client session
// ---------------------------------------------------------------------------
struct Session
{
    SOCKET   sock;
    uint64_t hdrKey = 0;
    uint64_t dllKey = 0;
    uint8_t  token[16]{};
    std::string hwid;
};

static auto RawSend = [](SOCKET s, const void* d, int n) -> bool {
    const char* p=(const char*)d; int sent=0;
    while(sent<n){int r=send(s,p+sent,n-sent,0);if(r<=0)return false;sent+=r;}return true;
};
static auto RawRecv = [](SOCKET s, void* d, int n, DWORD ms=10000) -> bool {
    setsockopt(s,SOL_SOCKET,SO_RCVTIMEO,(char*)&ms,sizeof(ms));
    char* p=(char*)d;int got=0;
    while(got<n){int r=recv(s,p+got,n-got,0);if(r<=0)return false;got+=r;}return true;
};

static bool SendMsg(Session& s, MsgType t, const void* pay=nullptr, uint16_t len=0)
{
    MsgHeader h{}; h.Magic=PROTO_MAGIC; h.Version=PROTO_VERSION; h.Type=t; h.Length=len;
    std::vector<uint8_t> buf(len);
    if (pay&&len) memcpy(buf.data(),pay,len);
    if (s.hdrKey&&!buf.empty()) XorStream(buf.data(),(uint32_t)buf.size(),s.hdrKey);
    uint32_t crc=Crc32((uint8_t*)&h,sizeof(h));
    if (!buf.empty()) crc^=Crc32(buf.data(),(uint32_t)buf.size());
    return RawSend(s.sock,&h,sizeof(h))&&(buf.empty()||RawSend(s.sock,buf.data(),(int)buf.size()))
        &&RawSend(s.sock,&crc,4);
}

static bool RecvMsg(Session& s, MsgType& t, std::vector<uint8_t>& pay, DWORD ms=10000)
{
    MsgHeader h{};
    if (!RawRecv(s.sock,&h,sizeof(h),ms)) return false;
    if (h.Magic!=PROTO_MAGIC||h.Version!=PROTO_VERSION) return false;
    pay.resize(h.Length);
    if (h.Length&&!RawRecv(s.sock,pay.data(),h.Length,ms)) return false;
    uint32_t rc{}; if (!RawRecv(s.sock,&rc,4,ms)) return false;
    uint32_t lc=Crc32((uint8_t*)&h,sizeof(h));
    if (!pay.empty()) lc^=Crc32(pay.data(),(uint32_t)pay.size());
    if (lc!=rc) return false;
    if (s.hdrKey&&!pay.empty()) XorStream(pay.data(),(uint32_t)pay.size(),s.hdrKey);
    t=h.Type; return true;
}

// ---------------------------------------------------------------------------
// StreamEncryptedDll — XOR-encrypt DLL with session-unique key, send chunks.
// Each session gets a different ciphertext even for the same DLL.
// ---------------------------------------------------------------------------
static bool StreamEncryptedDll(Session& s)
{
    if (g_dllBytes.empty()) return false;

    // Deep copy — we encrypt per-session so we need a fresh copy each time
    std::vector<uint8_t> enc = g_dllBytes;
    XorStream(enc.data(), (uint32_t)enc.size(), s.dllKey);

    uint32_t total  = (uint32_t)enc.size();
    uint32_t nChunks = (total + CHUNK_SIZE - 1) / CHUNK_SIZE;

    BinaryReadyPayload br{};
    br.TotalBytes = total;
    br.ChunkSize  = CHUNK_SIZE;
    br.NumChunks  = nChunks;
    if (!SendMsg(s, MsgType::BinaryReady, &br, sizeof(br)))
        return false;

    std::cout << std::format("[S] Streaming DLL to {} ({} chunks)\n",
                              s.hwid, nChunks);

    for (uint32_t c = 0; c < nChunks; ++c)
    {
        uint32_t off  = c * CHUNK_SIZE;
        uint32_t size = (uint32_t)std::min((size_t)CHUNK_SIZE,
                                            (size_t)(total - off));
        if (!SendMsg(s, MsgType::BinaryChunk, enc.data() + off, (uint16_t)size))
            return false;
    }

    return SendMsg(s, MsgType::BinaryEnd);
}

// ---------------------------------------------------------------------------
// Handle one client connection
// ---------------------------------------------------------------------------
static void HandleClient(SOCKET csock)
{
    Session s{csock};
    MsgType t{}; std::vector<uint8_t> pay;

    // 1. Receive Hello
    if (!RawRecv(s.sock, &(t=(MsgType)0, t), 0, 10000)) {} // drain / ignore
    MsgHeader h{};
    if (!RawRecv(s.sock, &h, sizeof(h), 10000)
        || h.Magic != PROTO_MAGIC || h.Version != PROTO_VERSION
        || h.Type != MsgType::Hello)
    {
        closesocket(csock); return;
    }

    pay.resize(h.Length);
    if (h.Length) RawRecv(s.sock, pay.data(), h.Length);
    uint32_t crc{}; RawRecv(s.sock, &crc, 4);

    if (pay.size() < sizeof(HelloPayload)) { closesocket(csock); return; }
    auto& hello = *(HelloPayload*)pay.data();
    s.hwid = HexStr(hello.HwidHash, 32);
    std::cout << std::format("[S] Hello HWID={} build={:08X}\n",
                              s.hwid, hello.BuildId);

    // 2. Build ID check
    if (hello.BuildId != CURRENT_BUILD)
    {
        std::cout << "[S] Reject: stale build\n";
        SendMsg(s, MsgType::AuthFail);
        closesocket(csock); return;
    }

    // 3. Paid status check
    {
        std::lock_guard lk(g_setMutex);
        bool inPaid      = g_paid.empty()      || g_paid.contains(s.hwid);
        bool inWhitelist = g_whitelist.empty() || g_whitelist.contains(s.hwid);
        if (!inPaid || !inWhitelist)
        {
            std::cout << std::format("[S] Reject: HWID {} not authorised\n", s.hwid);
            SendMsg(s, MsgType::AuthFail);
            closesocket(csock); return;
        }
    }

    // 4. Generate session token + derive keys
    srand((unsigned)(GetTickCount64() ^ (uintptr_t)&s));
    for (auto& b : s.token) b = (uint8_t)(rand() & 0xFF);
    s.hdrKey = DeriveKey(s.token, 0);
    s.dllKey = DeriveKey(s.token, 1);

    // 5. Send AuthOk
    AuthOkPayload aok{};
    memcpy(aok.SessionToken, s.token, 16);
    aok.ExpiresAt = (uint32_t)time(nullptr) + 3600;
    if (!SendMsg(s, MsgType::AuthOk, &aok, sizeof(aok)))
    { closesocket(csock); return; }
    std::cout << std::format("[S] AuthOk -> {}\n", s.hwid);

    // 6. Push config (optional)
    auto cfg = ReadFileFull(CONFIG_FILE);
    if (!cfg.empty())
        SendMsg(s, MsgType::Config, cfg.data(), (uint16_t)cfg.size());

    // 7. Stream encrypted DLL
    if (!g_dllBytes.empty())
    {
        if (!StreamEncryptedDll(s))
            std::cout << "[S] DLL stream failed\n";
        else
            std::cout << std::format("[S] DLL delivered to {}\n", s.hwid);
    }
    else
    {
        std::cout << "[S] No DLL loaded (place SparkyCore.dll next to server)\n";
    }

    // 8. Keep-alive loop
    while (true)
    {
        MsgType mt{}; std::vector<uint8_t> mp;
        if (!RecvMsg(s, mt, mp, 35000)) break;
        if (mt == MsgType::Heartbeat) SendMsg(s, MsgType::Ack);
    }

    std::cout << std::format("[S] {} disconnected\n", s.hwid);
    closesocket(csock);
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main()
{
    std::cout << "[SparkyServer] v1.0\n";

    // Load paid HWIDs and whitelist
    LoadSet(PAID_FILE,      g_paid);
    LoadSet(WHITELIST_FILE, g_whitelist);
    std::cout << std::format("[S] {} paid, {} whitelisted\n",
                              g_paid.size(), g_whitelist.size());

    // Pre-load DLL into memory
    g_dllBytes = ReadFileFull(DLL_FILE);
    if (g_dllBytes.empty())
        std::cout << "[S] WARNING: " << DLL_FILE << " not found — clients won't receive DLL\n";
    else
        std::cout << std::format("[S] DLL loaded ({} bytes)\n", g_dllBytes.size());

    WSADATA wsa{}; WSAStartup(MAKEWORD(2,2), &wsa);

    SOCKET ls = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in a{}; a.sin_family=AF_INET; a.sin_port=htons(LISTEN_PORT); a.sin_addr.s_addr=INADDR_ANY;
    int opt=1; setsockopt(ls,SOL_SOCKET,SO_REUSEADDR,(char*)&opt,sizeof(opt));

    if (bind(ls,(sockaddr*)&a,sizeof(a))!=0 || listen(ls,SOMAXCONN)!=0)
    { std::cerr << "[S] Failed to bind on port " << LISTEN_PORT << "\n"; return 1; }

    std::cout << std::format("[S] Listening on :{}\n", LISTEN_PORT);

    while (true)
    {
        sockaddr_in ca{}; int cl=sizeof(ca);
        SOCKET cs = accept(ls,(sockaddr*)&ca,&cl);
        if (cs == INVALID_SOCKET) continue;
        char ip[INET_ADDRSTRLEN]{}; inet_ntop(AF_INET,&ca.sin_addr,ip,sizeof(ip));
        std::cout << std::format("[S] Connection from {}\n", ip);
        std::thread(HandleClient, cs).detach();
    }

    closesocket(ls); WSACleanup(); return 0;
}
