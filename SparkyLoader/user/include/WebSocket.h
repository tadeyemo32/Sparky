#pragma once
// ---------------------------------------------------------------------------
// WebSocket.h — RFC 6455 framing helpers (shared by SparkyServer + SparkyLoader)
//
// After the initial HTTP handshake the protocol switches to WebSocket frames.
// This lets the GCP HTTP(S) Load Balancer and Cloud Run transparently proxy
// the binary protocol; a plain HTTP 200 + raw binary continuation is
// terminated by the LB immediately (Content-Length: 0).
//
// Server → Client frames: unmasked  (opcode 0x02 = binary)
// Client → Server frames: masked    (WebSocket spec §5.3 — server MUST close
//                                    if mask bit is 0 from a client)
// ---------------------------------------------------------------------------

#include "TlsLayer.h"
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <vector>
#include <string>
#include <cstdint>
#include <cstring>

// ---------------------------------------------------------------------------
// WsComputeAccept — RFC 6455 §4.2.2: SHA-1(key + magic) → base64
// ---------------------------------------------------------------------------
inline std::string WsComputeAccept(const std::string& clientKey)
{
    const std::string combined =
        clientKey + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

    uint8_t sha1[20]{};
    unsigned int outLen = sizeof(sha1);
    EVP_Digest(combined.data(), combined.size(),
               sha1, &outLen, EVP_sha1(), nullptr);

    // Base64 encode — EVP_EncodeBlock appends a null terminator and returns
    // the encoded length (28 chars for 20 bytes input).
    uint8_t b64[32]{};
    int b64len = EVP_EncodeBlock(b64, sha1, (int)sizeof(sha1));
    return std::string(reinterpret_cast<char*>(b64), (size_t)b64len);
}

// ---------------------------------------------------------------------------
// WsGenKey — generate a random 16-byte WebSocket client key (base64).
// Used by the loader when sending the Upgrade request.
// ---------------------------------------------------------------------------
inline std::string WsGenKey()
{
    uint8_t raw[16]{};
    RAND_bytes(raw, sizeof(raw));
    uint8_t b64[28]{};
    int len = EVP_EncodeBlock(b64, raw, sizeof(raw));
    return std::string(reinterpret_cast<char*>(b64), (size_t)len);
}

// ---------------------------------------------------------------------------
// WsSendFrame — send `data` wrapped in an unmasked WS binary frame.
// Used by the SERVER (server→client frames must NOT be masked per RFC 6455).
// ---------------------------------------------------------------------------
inline bool WsSendFrame(SOCKET sock, SSL* ssl, const void* data, size_t len)
{
    uint8_t hdr[10];
    int hlen = 0;
    hdr[hlen++] = 0x82; // FIN=1, opcode=binary(2)
    if (len <= 125) {
        hdr[hlen++] = (uint8_t)len;
    } else if (len <= 65535) {
        hdr[hlen++] = 0x7E;
        hdr[hlen++] = (uint8_t)(len >> 8);
        hdr[hlen++] = (uint8_t)(len & 0xFF);
    } else {
        hdr[hlen++] = 0x7F;
        for (int i = 7; i >= 0; --i)
            hdr[hlen++] = (uint8_t)(len >> (i * 8));
    }
    return NetSend(sock, ssl, hdr, hlen) &&
           NetSend(sock, ssl, data, (int)len);
}

// ---------------------------------------------------------------------------
// WsSendFrameMasked — send `data` wrapped in a masked WS binary frame.
// Used by the CLIENT (client→server frames MUST be masked per RFC 6455).
// Generates a fresh 4-byte random mask per frame via RAND_bytes.
// ---------------------------------------------------------------------------
inline bool WsSendFrameMasked(SOCKET sock, SSL* ssl, const void* data, size_t len)
{
    uint8_t mask[4]{};
    RAND_bytes(mask, 4);

    uint8_t hdr[14];
    int hlen = 0;
    hdr[hlen++] = 0x82; // FIN=1, opcode=binary(2)
    if (len <= 125) {
        hdr[hlen++] = 0x80 | (uint8_t)len;       // MASK bit set
    } else if (len <= 65535) {
        hdr[hlen++] = 0xFE;                        // MASK(1) | 0x7E
        hdr[hlen++] = (uint8_t)(len >> 8);
        hdr[hlen++] = (uint8_t)(len & 0xFF);
    } else {
        hdr[hlen++] = 0xFF;                        // MASK(1) | 0x7F
        for (int i = 7; i >= 0; --i)
            hdr[hlen++] = (uint8_t)(len >> (i * 8));
    }
    hdr[hlen++] = mask[0];
    hdr[hlen++] = mask[1];
    hdr[hlen++] = mask[2];
    hdr[hlen++] = mask[3];

    if (!NetSend(sock, ssl, hdr, hlen)) return false;

    // XOR payload with cycling mask
    std::vector<uint8_t> masked(len);
    const uint8_t* src = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < len; ++i)
        masked[i] = src[i] ^ mask[i & 3];
    return NetSend(sock, ssl, masked.data(), (int)masked.size());
}

// ---------------------------------------------------------------------------
// WsRecvFrame — receive one complete WebSocket frame and return its payload.
// Handles both masked (client→server) and unmasked (server→client) frames.
// Ignores continuation / ping / pong / close opcodes for simplicity.
// ---------------------------------------------------------------------------
#ifdef _WIN32
inline bool WsRecvFrame(SOCKET sock, SSL* ssl,
                         std::vector<uint8_t>& out, DWORD ms = 10000)
#else
inline bool WsRecvFrame(SOCKET sock, SSL* ssl,
                         std::vector<uint8_t>& out, unsigned int ms = 10000)
#endif
{
    uint8_t h2[2];
    if (!NetRecv(sock, ssl, h2, 2, ms)) return false;

    bool   masked  = (h2[1] & 0x80) != 0;
    uint64_t paylen = h2[1] & 0x7F;

    if (paylen == 126) {
        uint8_t ext[2];
        if (!NetRecv(sock, ssl, ext, 2, ms)) return false;
        paylen = ((uint64_t)ext[0] << 8) | ext[1];
    } else if (paylen == 127) {
        uint8_t ext[8];
        if (!NetRecv(sock, ssl, ext, 8, ms)) return false;
        paylen = 0;
        for (int i = 0; i < 8; ++i) paylen = (paylen << 8) | ext[i];
    }

    uint8_t maskKey[4]{};
    if (masked && !NetRecv(sock, ssl, maskKey, 4, ms)) return false;

    out.resize((size_t)paylen);
    if (paylen > 0 && !NetRecv(sock, ssl, out.data(), (int)paylen, ms))
        return false;

    if (masked) {
        for (size_t i = 0; i < out.size(); ++i)
            out[i] ^= maskKey[i & 3];
    }
    return true;
}
