#pragma once
#include <cstdint>

// Shared binary wire protocol — SparkyLoader <-> SparkyServer
// All integers little-endian.

static constexpr uint32_t PROTO_MAGIC   = 0x53504B59; // "SPKY"
static constexpr uint8_t  PROTO_VERSION = 1;

enum class MsgType : uint8_t
{
    // Loader -> Server
    Hello        = 0x01,  // HWID + build ID
    Heartbeat    = 0x02,  // keep-alive

    // Server -> Loader
    AuthOk       = 0x10,  // session token + expiry
    AuthFail     = 0x11,  // access denied
    Config       = 0x12,  // feature config blob
    BinaryReady  = 0x13,  // total encrypted DLL size (uint32_t payload)
    BinaryChunk  = 0x14,  // encrypted DLL chunk (up to 4 KB)
    BinaryEnd    = 0x15,  // all chunks sent, transfer complete
    Kick         = 0x16,  // force disconnect

    // Bidirectional
    Ack          = 0x20,
    Error        = 0xFF,
};

#pragma pack(push, 1)
struct MsgHeader
{
    uint32_t Magic;    // PROTO_MAGIC
    uint8_t  Version;  // PROTO_VERSION
    MsgType  Type;
    uint16_t Length;   // payload byte count
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
    uint8_t  SessionToken[16]; // used to XOR-decrypt the DLL stream
    uint32_t ExpiresAt;        // unix timestamp
};

struct BinaryReadyPayload
{
    uint32_t TotalBytes;   // total size of the encrypted DLL
    uint32_t ChunkSize;    // bytes per BinaryChunk (usually 4096)
    uint32_t NumChunks;
};
#pragma pack(pop)

// ---------------------------------------------------------------------------
// Crypto helpers
// ---------------------------------------------------------------------------

// XOR stream cipher — obfuscates wire data.
// key is a 64-bit seed rotated left each byte.
inline void XorStream(uint8_t* data, uint32_t len, uint64_t key)
{
    for (uint32_t i = 0; i < len; ++i)
    {
        data[i] ^= static_cast<uint8_t>(key >> ((i % 8) * 8));
        key = (key << 1) | (key >> 63);
    }
}

// Derive a 64-bit XOR key from a 16-byte session token.
// Also mixes in a salt so the DLL key differs from the header key.
inline uint64_t DeriveKey(const uint8_t token[16], uint32_t salt = 0)
{
    uint64_t k = 0;
    for (int i = 0; i < 8; ++i)
        k ^= (uint64_t)token[i] << (i * 8);
    for (int i = 8; i < 16; ++i)
        k ^= (uint64_t)token[i] << ((i - 8) * 8);
    // Mix in salt to produce different keys for header vs. DLL stream
    k ^= ((uint64_t)salt << 32) | salt;
    return k;
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
