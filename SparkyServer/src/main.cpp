// SparkyServer — binary protocol auth + config server
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

#pragma comment(lib, "ws2_32.lib")

#include "../../SparkyLoader/include/Protocol.h"

static constexpr uint16_t LISTEN_PORT   = 7777;
static constexpr uint32_t CURRENT_BUILD = 0x0001'0000;
static constexpr const char* CONFIG_FILE = "config.bin";
static constexpr const char* WHITELIST   = "hwid_whitelist.txt";

static std::unordered_set<std::string> g_hwids;
static std::mutex g_mutex;

static std::string HexStr(const uint8_t* d, size_t n)
{
    static const char h[] = "0123456789abcdef";
    std::string out; out.reserve(n * 2);
    for (size_t i = 0; i < n; ++i)
    { out += h[d[i]>>4]; out += h[d[i]&0xF]; }
    return out;
}

static std::vector<uint8_t> ReadFile(const char* path)
{
    std::ifstream f(path, std::ios::binary|std::ios::ate);
    if (!f.is_open()) return {};
    std::vector<uint8_t> buf(f.tellg()); f.seekg(0);
    f.read((char*)buf.data(), buf.size());
    return buf;
}

static void LoadWhitelist()
{
    std::ifstream f(WHITELIST);
    if (!f.is_open()) return;
    std::string ln; std::lock_guard lk(g_mutex);
    while (std::getline(f, ln))
        if (!ln.empty()) g_hwids.insert(ln);
}

// ---------------------------------------------------------------------------
// Per-client session
// ---------------------------------------------------------------------------
struct Session { SOCKET sock; uint64_t xorKey = 0; std::string hwid; };

static auto RawSend = [](SOCKET s, const void* d, int n) -> bool {
    const char* p = (const char*)d; int sent = 0;
    while (sent < n) { int r = send(s, p+sent, n-sent, 0); if (r<=0) return false; sent+=r; }
    return true;
};
static auto RawRecv = [](SOCKET s, void* d, int n, DWORD ms) -> bool {
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (char*)&ms, sizeof(ms));
    char* p = (char*)d; int got = 0;
    while (got < n) { int r = recv(s, p+got, n-got, 0); if (r<=0) return false; got+=r; }
    return true;
};

static bool SendMsg(Session& s, MsgType t, const void* pay=nullptr, uint16_t len=0)
{
    MsgHeader h{}; h.Magic=PROTO_MAGIC; h.Version=PROTO_VERSION; h.Type=t; h.Length=len;
    std::vector<uint8_t> buf(len);
    if (pay && len) memcpy(buf.data(), pay, len);
    if (s.xorKey && !buf.empty()) XorStream(buf.data(), buf.size(), s.xorKey);
    uint32_t crc = Crc32((uint8_t*)&h, sizeof(h));
    if (!buf.empty()) crc ^= Crc32(buf.data(), buf.size());
    return RawSend(s.sock,&h,sizeof(h)) && (buf.empty()||RawSend(s.sock,buf.data(),buf.size()))
        && RawSend(s.sock,&crc,4);
}

static bool RecvMsg(Session& s, MsgType& t, std::vector<uint8_t>& pay, DWORD ms=10000)
{
    MsgHeader h{};
    if (!RawRecv(s.sock,&h,sizeof(h),ms)) return false;
    if (h.Magic!=PROTO_MAGIC||h.Version!=PROTO_VERSION) return false;
    pay.resize(h.Length);
    if (h.Length && !RawRecv(s.sock,pay.data(),h.Length,ms)) return false;
    uint32_t rc{}; if (!RawRecv(s.sock,&rc,4,ms)) return false;
    uint32_t lc = Crc32((uint8_t*)&h,sizeof(h));
    if (!pay.empty()) lc ^= Crc32(pay.data(),pay.size());
    if (lc!=rc) return false;
    if (s.xorKey && !pay.empty()) XorStream(pay.data(),pay.size(),s.xorKey);
    t = h.Type; return true;
}

static void HandleClient(SOCKET csock)
{
    Session s{csock};
    MsgType t{}; std::vector<uint8_t> pay;

    if (!RecvMsg(s,t,pay) || t!=MsgType::Hello || pay.size()<sizeof(HelloPayload))
    { closesocket(csock); return; }

    auto& hello = *(HelloPayload*)pay.data();
    s.hwid = HexStr(hello.HwidHash, 32);
    std::cout << std::format("[S] Hello HWID={} build={:08X}\n", s.hwid, hello.BuildId);

    if (hello.BuildId != CURRENT_BUILD)
    { std::cout << "[S] Rejected: old build\n"; SendMsg(s,MsgType::AuthFail); closesocket(csock); return; }

    { std::lock_guard lk(g_mutex);
      if (!g_hwids.empty() && !g_hwids.contains(s.hwid))
      { std::cout << "[S] Rejected: not whitelisted\n"; SendMsg(s,MsgType::AuthFail); closesocket(csock); return; } }

    // Build token + derive xor key
    AuthOkPayload auth{};
    srand((unsigned)(GetTickCount64()^(uintptr_t)&s));
    for (auto& b : auth.SessionToken) b=(uint8_t)(rand()&0xFF);
    auth.ExpiresAt = (uint32_t)time(nullptr)+3600;
    memcpy(&s.xorKey, auth.SessionToken, 8);

    if (!SendMsg(s,MsgType::AuthOk,&auth,sizeof(auth)))
    { closesocket(csock); return; }
    std::cout << std::format("[S] AuthOk -> {}\n", s.hwid);

    // Push config
    auto cfg = ReadFile(CONFIG_FILE);
    if (!cfg.empty())
    {
        if (!SendMsg(s,MsgType::Config,cfg.data(),(uint16_t)cfg.size()))
        { closesocket(csock); return; }
        std::cout << std::format("[S] Config pushed ({} bytes)\n", cfg.size());
    }

    // Keep-alive loop
    while (true)
    {
        if (!RecvMsg(s,t,pay,30000)) break;
        if (t==MsgType::Heartbeat) SendMsg(s,MsgType::Ack);
    }
    std::cout << std::format("[S] {} disconnected\n", s.hwid);
    closesocket(csock);
}

int main()
{
    std::cout << "[SparkyServer] Marvel Rivals Clone — Auth Server\n";
    LoadWhitelist();
    std::cout << std::format("[S] {} whitelisted HWIDs\n", g_hwids.size());

    WSADATA wsa{}; WSAStartup(MAKEWORD(2,2),&wsa);
    SOCKET ls = socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);

    sockaddr_in a{}; a.sin_family=AF_INET; a.sin_port=htons(LISTEN_PORT); a.sin_addr.s_addr=INADDR_ANY;
    int opt=1; setsockopt(ls,SOL_SOCKET,SO_REUSEADDR,(char*)&opt,sizeof(opt));

    if (bind(ls,(sockaddr*)&a,sizeof(a))!=0||listen(ls,SOMAXCONN)!=0)
    { std::cerr << "[S] bind/listen failed\n"; return 1; }
    std::cout << std::format("[S] Listening on :{}\n", LISTEN_PORT);

    while (true)
    {
        sockaddr_in ca{}; int cl=sizeof(ca);
        SOCKET cs = accept(ls,(sockaddr*)&ca,&cl);
        if (cs==INVALID_SOCKET) continue;
        char ip[INET_ADDRSTRLEN]{}; inet_ntop(AF_INET,&ca.sin_addr,ip,sizeof(ip));
        std::cout << std::format("[S] Connection from {}\n",ip);
        std::thread(HandleClient,cs).detach();
    }
    closesocket(ls); WSACleanup(); return 0;
}
