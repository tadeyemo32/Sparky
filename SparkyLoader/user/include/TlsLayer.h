#pragma once
// ---------------------------------------------------------------------------
// TlsLayer — transparent NetSend / NetRecv wrappers.
//
// When ssl != nullptr the call routes through SSL_write / SSL_read.
// When ssl == nullptr it falls back to plain Winsock send / recv.
// This lets the same SendMsg / RecvMsg code work in both TLS and
// plaintext modes without any structural changes.
//
// Used by both SparkyServer and SparkyLoader.
// Link: OpenSSL::SSL  OpenSSL::Crypto  (CMake), or libssl / libcrypto.
// ---------------------------------------------------------------------------
#ifdef _WIN32
#  include <WinSock2.h>
#else
#  include <sys/socket.h>
#  include <netinet/in.h>
#  include <arpa/inet.h>
#  include <unistd.h>
   using SOCKET = int;
   static constexpr SOCKET INVALID_SOCKET = -1;
   inline int closesocket(SOCKET s) { return ::close(s); }
#endif

#include <openssl/ssl.h>
#include <openssl/err.h>
#include <string>

// Send exactly `len` bytes. Returns false on any partial write or error.
inline bool NetSend(SOCKET sock, SSL* ssl, const void* data, int len)
{
    const char* p = static_cast<const char*>(data);
    int sent = 0;
    while (sent < len)
    {
        int r = ssl ? SSL_write(ssl,  p + sent, len - sent)
                    : send   (sock,  p + sent, len - sent, 0);
        if (r <= 0) return false;
        sent += r;
    }
    return true;
}

// Receive exactly `len` bytes with a socket-level timeout (`ms`, 0 = leave unchanged).
// Returns false on timeout or any error.
#ifdef _WIN32
inline bool NetRecv(SOCKET sock, SSL* ssl, void* data, int len, DWORD ms = 10000)
{
    if (ms)
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO,
                   reinterpret_cast<const char*>(&ms), sizeof(ms));
#else
inline bool NetRecv(SOCKET sock, SSL* ssl, void* data, int len, unsigned int ms = 10000)
{
    if (ms)
    {
        struct timeval tv{ (time_t)(ms / 1000), (suseconds_t)((ms % 1000) * 1000) };
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO,
                   reinterpret_cast<const char*>(&tv), sizeof(tv));
    }
#endif
    char* p = static_cast<char*>(data);
    int got = 0;
    while (got < len)
    {
        int r = ssl ? SSL_read(ssl,  p + got, len - got)
                    : recv   (sock,  p + got, len - got, 0);
        if (r <= 0) return false;
        got += r;
    }
    return true;
}

// Returns the last OpenSSL error string, or an empty string if none.
inline std::string TlsLastError()
{
    char buf[256]{};
    unsigned long e = ERR_get_error();
    if (e == 0) return {};
    ERR_error_string_n(e, buf, sizeof(buf));
    return buf;
}
