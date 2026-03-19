#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>

// =============================================================================
// Shared binary wire protocol — SparkyLoader <-> SparkyServer
// All integers little-endian.  Single #pragma pack block covers all structs.
// =============================================================================

static constexpr uint32_t PROTO_MAGIC   = 0x53504B59; // "SPKY"
static constexpr uint8_t  PROTO_VERSION = 3;          // v3: rolling key, nonce HB

// ---------------------------------------------------------------------------
// Message types
// ---------------------------------------------------------------------------
enum class MsgType : uint8_t
{
    // Loader → Server
    Hello        = 0x01,  // HelloPayload: HWID + BuildId + LoaderHash
    Heartbeat    = 0x02,  // HeartbeatPayload: 16-byte nonce (during transfer)
                          //   or zero nonce (post-delivery keep-alive)

    // Server → Loader
    AuthOk       = 0x10,  // AuthOkPayload: session token + expiry  (sent PLAIN)
    AuthFail     = 0x11,  // no payload — access denied
    Config       = 0x12,  // opaque config blob
    BinaryReady  = 0x13,  // BinaryReadyPayload: size / chunk count / HB interval
    BinaryChunk  = 0x14,  // raw chunk, encrypted with current rolling key
    BinaryEnd    = 0x15,  // transfer complete
    Kick         = 0x16,  // server is kicking this session

    // Bidirectional
    Ack          = 0x20,  // generic acknowledgement
    Error        = 0xFF,
};

// ---------------------------------------------------------------------------
// Wire structures  (all packed, little-endian)
// ---------------------------------------------------------------------------
#pragma pack(push, 1)

struct MsgHeader
{
    uint32_t Magic;     // PROTO_MAGIC
    uint8_t  Version;   // PROTO_VERSION
    MsgType  Type;
    uint16_t Length;    // payload byte count (0 = no payload)
    uint8_t  Pad[4];    // reserved, must be zero
};
static_assert(sizeof(MsgHeader) == 12, "MsgHeader must be 12 bytes");

// Loader → Server (plain, no session key yet)
struct HelloPayload
{
    uint8_t  HwidHash[32];   // SHA-256(MachineGuid)
    uint32_t BuildId;        // must match server's CURRENT_BUILD
    uint8_t  LoaderHash[32]; // SHA-256 of loader binary on disk
    uint64_t Timestamp;      // Unix timestamp (anti-replay)
};

// Server → Loader (ALWAYS SENT PLAIN — before session keys are set)
// The loader reads this raw to extract the token, then derives its keys.
struct AuthOkPayload
{
    uint8_t  SessionToken[16]; // random 16 bytes — used to derive hdrKey and dllKey
    uint32_t ExpiresAt;        // unix timestamp
};

// Server → Loader: announces the incoming DLL transfer
struct BinaryReadyPayload
{
    uint32_t TotalBytes;         // total plaintext DLL size
    uint32_t ChunkSize;          // bytes per BinaryChunk (usually 4096)
    uint32_t NumChunks;
    uint32_t ChunksPerHeartbeat; // loader MUST send a HeartbeatPayload after
                                  // every N chunks (and after the final chunk)
};

// Loader → Server: sent after each ChunksPerHeartbeat batch AND during
// post-delivery keep-alive.  Nonce drives key rotation.
// For post-delivery keep-alives, Nonce may be all zeros.
struct HeartbeatPayload
{
    uint8_t Nonce[16]; // CryptGenRandom bytes — seeds next RollKey() call
};

#pragma pack(pop)

// Server must receive a Heartbeat within this many milliseconds after each batch.
static constexpr uint32_t HEARTBEAT_DEADLINE_MS = 30000; // 30 s

// ---------------------------------------------------------------------------
// Crypto helpers — all inline, no external deps
// ---------------------------------------------------------------------------

namespace detail {
    inline void sha256_block(const uint8_t* data, size_t len, uint8_t out[32]);
}

// SHA-256 Counter Mode (CTR) stream cipher.
// Key is a 64-bit seed; we use SHA-256 to generate cryptographically secure keystreams.
inline void XorStream(uint8_t* data, uint32_t len, uint64_t key)
{
    uint8_t input[24] = {0};
    for (int i=0; i<8; ++i) input[i] = (uint8_t)(key >> (i*8));

    uint64_t counter = 0;
    uint8_t keystream[32];
    uint32_t blockPos = 32;

    for (uint32_t i = 0; i < len; ++i)
    {
        if (blockPos == 32)
        {
            for (int k=0; k<8; ++k) input[8+k] = (uint8_t)(counter >> (k*8));
            detail::sha256_block(input, 16, keystream);
            blockPos = 0;
            counter++;
        }
        data[i] ^= keystream[blockPos++];
    }
}

// Derive a 64-bit session key from a 16-byte token.
// salt=0 → header key,  salt=1 → initial DLL key
inline uint64_t DeriveKey(const uint8_t token[16], uint32_t salt = 0)
{
    uint64_t k = 0;
    for (int i = 0; i < 8;  ++i) k ^= (uint64_t)token[i]   << (i * 8);
    for (int i = 8; i < 16; ++i) k ^= (uint64_t)token[i]   << ((i - 8) * 8);
    k ^= ((uint64_t)salt << 32) | salt;
    return k;
}

// ---------------------------------------------------------------------------
// SHA-256 — pure C++, no external deps.
// Only handles input ≤ 55 bytes (single 64-byte block after padding).
// Used by RollKey() whose input is always exactly 24 bytes — safe.
// ---------------------------------------------------------------------------
namespace detail {
    inline void sha256_block(const uint8_t* data, size_t len, uint8_t out[32])
    {
        static const uint32_t K[64] = {
            0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,
            0x923f82a4,0xab1c5ed5,0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,
            0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,0xe49b69c1,0xefbe4786,
            0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
            0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,
            0x06ca6351,0x14292967,0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,
            0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,0xa2bfe8a1,0xa81a664b,
            0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
            0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,
            0x5b9cca4f,0x682e6ff3,0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,
            0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
        };
        uint32_t h[8] = {
            0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,
            0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19
        };
        uint8_t buf[64]{};
        memcpy(buf, data, len);
        buf[len] = 0x80;
        uint64_t bits = (uint64_t)len * 8;
        for (int i = 0; i < 8; ++i)
            buf[63 - i] = (uint8_t)(bits >> (i * 8));

        auto ror32 = [](uint32_t x, int n){ return (x >> n) | (x << (32-n)); };
        uint32_t w[64]{};
        for (int i = 0; i < 16; ++i)
            w[i] = ((uint32_t)buf[i*4]   << 24) | ((uint32_t)buf[i*4+1] << 16)
                 | ((uint32_t)buf[i*4+2] <<  8) |  (uint32_t)buf[i*4+3];
        for (int i = 16; i < 64; ++i)
        {
            uint32_t s0 = ror32(w[i-15],7) ^ ror32(w[i-15],18) ^ (w[i-15] >> 3);
            uint32_t s1 = ror32(w[i- 2],17)^ ror32(w[i- 2],19) ^ (w[i- 2] >> 10);
            w[i] = w[i-16] + s0 + w[i-7] + s1;
        }
        uint32_t a=h[0],b=h[1],c=h[2],d=h[3],e=h[4],f=h[5],g=h[6],hh=h[7];
        for (int i = 0; i < 64; ++i)
        {
            uint32_t S1   = ror32(e,6) ^ ror32(e,11) ^ ror32(e,25);
            uint32_t ch   = (e & f) ^ (~e & g);
            uint32_t tmp1 = hh + S1 + ch + K[i] + w[i];
            uint32_t S0   = ror32(a,2) ^ ror32(a,13) ^ ror32(a,22);
            uint32_t maj  = (a & b) ^ (a & c) ^ (b & c);
            uint32_t tmp2 = S0 + maj;
            hh=g; g=f; f=e; e=d+tmp1; d=c; c=b; b=a; a=tmp1+tmp2;
        }
        h[0]+=a; h[1]+=b; h[2]+=c; h[3]+=d; h[4]+=e; h[5]+=f; h[6]+=g; h[7]+=hh;
        for (int i = 0; i < 8; ++i) {
            out[i*4  ] = (uint8_t)(h[i] >> 24); out[i*4+1] = (uint8_t)(h[i] >> 16);
            out[i*4+2] = (uint8_t)(h[i] >>  8); out[i*4+3] = (uint8_t)(h[i]);
        }
    }
} // namespace detail

// ---------------------------------------------------------------------------
// RollKey — advances the XOR key after each heartbeat batch.
//
// Both endpoints call RollKey(current, nonce) identically and in the same
// order, so they stay perfectly in sync.  One wrong or missing nonce causes
// key divergence — all subsequent chunks decrypt to garbage.
//
// input  = currentKey[8 bytes LE] || nonce[16 bytes]  (24 bytes total)
// output = SHA-256(input)[0:8]  interpreted as uint64_t LE
// ---------------------------------------------------------------------------
inline uint64_t RollKey(uint64_t currentKey, const uint8_t nonce[16])
{
    uint8_t input[24];
    for (int i = 0; i < 8; ++i) input[i] = (uint8_t)(currentKey >> (i * 8));
    memcpy(input + 8, nonce, 16);

    uint8_t digest[32]{};
    detail::sha256_block(input, 24, digest);

    uint64_t next = 0;
    for (int i = 0; i < 8; ++i) next |= (uint64_t)digest[i] << (i * 8);
    return next;
}

// ---------------------------------------------------------------------------
// CRC-32 (Ethernet polynomial 0xEDB88320) — message integrity check
// ---------------------------------------------------------------------------
inline uint32_t Crc32(const uint8_t* data, uint32_t len)
{
    uint32_t crc = 0xFFFFFFFFu;
    while (len--)
    {
        crc ^= *data++;
        for (int k = 0; k < 8; ++k)
            crc = (crc >> 1) ^ (0xEDB88320u & -(crc & 1u));
    }
    return ~crc;
}
