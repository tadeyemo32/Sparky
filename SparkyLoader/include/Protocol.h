#pragma once
#include <cstdint>

// Shared binary wire protocol — Loader <-> Server
// All integers little-endian.

static constexpr uint32_t PROTO_MAGIC   = 0x53504B59; // "SPKY"
static constexpr uint8_t  PROTO_VERSION = 1;

enum class MsgType : uint8_t
{
    Hello     = 0x01,  // Loader -> Server: HWID + build
    Heartbeat = 0x02,  // Loader -> Server: keep-alive
    AuthOk    = 0x10,  // Server -> Loader: session token
    AuthFail  = 0x11,  // Server -> Loader: rejected
    Config    = 0x12,  // Server -> Loader: config blob
    Kick      = 0x13,  // Server -> Loader: disconnect
    Ack       = 0x20,
    Error     = 0xFF,
};

#pragma pack(push, 1)
struct MsgHeader
{
    uint32_t Magic;
    uint8_t  Version;
    MsgType  Type;
    uint16_t Length;   // payload bytes
    uint8_t  Pad[4];   // reserved
};
static_assert(sizeof(MsgHeader) == 12);

struct HelloPayload
{
    uint8_t  HwidHash[32]; // SHA-256 of machine GUID
    uint32_t BuildId;
};

struct AuthOkPayload
{
    uint8_t  SessionToken[16];
    uint32_t ExpiresAt;
};
#pragma pack(pop)

// Simple XOR stream (obfuscates wire data; not a substitute for TLS)
inline void XorStream(uint8_t* data, uint32_t len, uint64_t key)
{
    for (uint32_t i = 0; i < len; ++i)
    {
        data[i] ^= static_cast<uint8_t>(key >> ((i % 8) * 8));
        key = (key << 1) | (key >> 63);
    }
}

// CRC-32 (Ethernet polynomial 0xEDB88320)
inline uint32_t Crc32(const uint8_t* data, uint32_t len)
{
    uint32_t crc = 0xFFFFFFFF;
    while (len--)
    {
        crc ^= *data++;
        for (int k = 0; k < 8; ++k)
            crc = (crc >> 1) ^ (0xEDB88320u & -(crc & 1u));
    }
    return ~crc;
}
