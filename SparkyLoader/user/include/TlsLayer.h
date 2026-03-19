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
#include <WinSock2.h>
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
inline bool NetRecv(SOCKET sock, SSL* ssl, void* data, int len, DWORD ms = 10000)
{
    if (ms)
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO,
                   reinterpret_cast<const char*>(&ms), sizeof(ms));
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
