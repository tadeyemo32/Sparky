// test_protocol.cpp
// Unit tests for all inline functions in Protocol.h:
//   Crc32, XorStream, DeriveKey, RollKey
// These are pure C++ — no Windows API, no sockets, no crypto hardware.
#include "TestRunner.h"
#include "Protocol.h"

#include <cstring>
#include <array>

// =============================================================================
// CRC-32
// =============================================================================

TEST("crc32/empty_buffer_is_zero")
{
    // CRC of zero bytes: ~0xFFFFFFFF == 0
    EXPECT_EQ(Crc32(nullptr, 0), 0x00000000u);

    const uint8_t dummy[1]{};
    EXPECT_EQ(Crc32(dummy, 0), 0x00000000u);
}

TEST("crc32/standard_vector_123456789")
{
    // The standard CRC-32/ISO-HDLC check value for "123456789".
    const uint8_t data[] = {'1','2','3','4','5','6','7','8','9'};
    EXPECT_EQ(Crc32(data, 9), 0xCBF43926u);
}

TEST("crc32/single_byte_A")
{
    // CRC of 0x41 ('A'): verified against standard table.
    const uint8_t data[] = {0x41};
    EXPECT_EQ(Crc32(data, 1), 0xD3D99E8Bu);
}

TEST("crc32/deterministic")
{
    const uint8_t data[] = {0x01, 0x02, 0x03, 0x04, 0xFF};
    EXPECT_EQ(Crc32(data, sizeof(data)), Crc32(data, sizeof(data)));
}

TEST("crc32/different_inputs_different_results")
{
    const uint8_t a[] = {0xAA, 0xBB};
    const uint8_t b[] = {0xAA, 0xBC}; // one byte different
    EXPECT_NE(Crc32(a, sizeof(a)), Crc32(b, sizeof(b)));
}

TEST("crc32/byte_order_matters")
{
    const uint8_t fwd[] = {0x01, 0x02, 0x03};
    const uint8_t rev[] = {0x03, 0x02, 0x01};
    EXPECT_NE(Crc32(fwd, 3), Crc32(rev, 3));
}

// =============================================================================
// XorStream
// =============================================================================

TEST("xorstream/roundtrip_restores_plaintext")
{
    const uint8_t plain[] = {0xDE, 0xAD, 0xBE, 0xEF, 0x01, 0x23, 0x45, 0x67};
    uint8_t buf[sizeof(plain)];
    memcpy(buf, plain, sizeof(plain));

    const uint64_t key = 0x0123456789ABCDEFull;
    XorStream(buf, sizeof(buf), key);
    XorStream(buf, sizeof(buf), key); // second pass must restore plaintext

    EXPECT_EQ(memcmp(buf, plain, sizeof(plain)), 0);
}

TEST("xorstream/zero_key_leaves_data_unchanged")
{
    const uint8_t plain[] = {0x11, 0x22, 0x33, 0x44};
    uint8_t buf[sizeof(plain)];
    memcpy(buf, plain, sizeof(plain));

    XorStream(buf, sizeof(buf), 0);

    // XOR with 0 on every byte → no change
    EXPECT_EQ(memcmp(buf, plain, sizeof(plain)), 0);
}

TEST("xorstream/non_zero_key_changes_data")
{
    const uint8_t plain[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    uint8_t buf[sizeof(plain)];
    memcpy(buf, plain, sizeof(plain));

    XorStream(buf, sizeof(buf), 0xDEADBEEFCAFEBABEull);

    EXPECT_NE(memcmp(buf, plain, sizeof(plain)), 0);
}

TEST("xorstream/single_byte")
{
    uint8_t b = 0xAB;
    // key = 0xCD: low byte of key is 0xCD; 0xAB ^ 0xCD == 0x66
    XorStream(&b, 1, 0xCDull);
    EXPECT_EQ(b, (uint8_t)(0xAB ^ 0xCD));
}

TEST("xorstream/output_size_equals_input_size")
{
    // XorStream must not write beyond the buffer.
    // Guard byte after the buffer should remain unchanged.
    uint8_t buf[9]; // 8 test bytes + 1 guard
    memset(buf, 0xAA, sizeof(buf));
    buf[8] = 0x55; // guard

    XorStream(buf, 8, 0x1234567890ABCDEFull);

    EXPECT_EQ(buf[8], 0x55); // guard must be untouched
}

TEST("xorstream/large_buffer_roundtrip")
{
    // 256 bytes with rolling key — all must round-trip cleanly.
    uint8_t data[256];
    for (int i = 0; i < 256; ++i) data[i] = (uint8_t)i;

    const uint64_t key = 0xFEDCBA9876543210ull;
    uint8_t enc[256];
    memcpy(enc, data, 256);
    XorStream(enc, 256, key);

    EXPECT_NE(memcmp(enc, data, 256), 0); // must have changed something

    XorStream(enc, 256, key); // decrypt
    EXPECT_EQ(memcmp(enc, data, 256), 0); // must be identical to original
}

// =============================================================================
// DeriveKey
// =============================================================================

TEST("derivekey/deterministic_same_token")
{
    const uint8_t token[16] = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16};
    EXPECT_EQ(DeriveKey(token, 0), DeriveKey(token, 0));
    EXPECT_EQ(DeriveKey(token, 1), DeriveKey(token, 1));
}

TEST("derivekey/different_salts_produce_different_keys")
{
    const uint8_t token[16] = {0xAA,0xBB,0xCC,0xDD,0xEE,0xFF,
                                0x11,0x22,0x33,0x44,0x55,0x66,
                                0x77,0x88,0x99,0x00};
    EXPECT_NE(DeriveKey(token, 0), DeriveKey(token, 1));
}

TEST("derivekey/all_zero_token_is_deterministic")
{
    const uint8_t zero[16]{};
    const uint64_t k0 = DeriveKey(zero, 0);
    EXPECT_EQ(k0, DeriveKey(zero, 0)); // same each call
}

TEST("derivekey/different_tokens_produce_different_keys")
{
    const uint8_t t1[16] = {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
    const uint8_t t2[16] = {2,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
    EXPECT_NE(DeriveKey(t1, 0), DeriveKey(t2, 0));
}

TEST("derivekey/header_and_dll_key_differ")
{
    // The session key (salt=0) and DLL key (salt=1) must never be the same.
    const uint8_t token[16] = {0x10,0x20,0x30,0x40,0x50,0x60,0x70,0x80,
                                0x90,0xA0,0xB0,0xC0,0xD0,0xE0,0xF0,0x00};
    EXPECT_NE(DeriveKey(token, 0), DeriveKey(token, 1));
}

// =============================================================================
// RollKey
// =============================================================================

TEST("rollkey/deterministic_same_inputs")
{
    const uint64_t key   = 0xDEADBEEFCAFEBABEull;
    const uint8_t nonce[16] = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16};
    EXPECT_EQ(RollKey(key, nonce), RollKey(key, nonce));
}

TEST("rollkey/advances_the_key")
{
    const uint64_t key   = 0xABCDEF0123456789ull;
    const uint8_t nonce[16] = {0xFF,0xFE,0xFD,0xFC,0xFB,0xFA,
                                0xF9,0xF8,0xF7,0xF6,0xF5,0xF4,
                                0xF3,0xF2,0xF1,0xF0};
    EXPECT_NE(RollKey(key, nonce), key);
}

TEST("rollkey/different_nonces_produce_different_keys")
{
    const uint64_t key = 0x1234567890ABCDEFull;
    const uint8_t n1[16] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1};
    const uint8_t n2[16] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,2};
    EXPECT_NE(RollKey(key, n1), RollKey(key, n2));
}

TEST("rollkey/zero_nonce_is_deterministic")
{
    const uint64_t key = 0x0000000000000001ull;
    const uint8_t zero[16]{};
    EXPECT_EQ(RollKey(key, zero), RollKey(key, zero));
}

TEST("rollkey/chained_rolls_diverge_on_different_nonce")
{
    // Simulates server and client diverging when the client sends a wrong nonce.
    const uint64_t start = 0xCAFECAFECAFECAFEull;
    const uint8_t good[16] = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16};
    const uint8_t bad [16] = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15, 0}; // 1 bit different

    uint64_t server = RollKey(start, good);
    uint64_t client = RollKey(start, bad);

    // Keys have already diverged after one roll
    EXPECT_NE(server, client);

    // They keep diverging on subsequent identical rolls
    const uint8_t next[16] = {0xAA,0xBB,0xCC,0xDD,
                               0xEE,0xFF,0x11,0x22,
                               0x33,0x44,0x55,0x66,
                               0x77,0x88,0x99,0x00};
    EXPECT_NE(RollKey(server, next), RollKey(client, next));
}

// =============================================================================
// Wire struct sizes and protocol constants
// =============================================================================

TEST("protocol/MsgHeader_is_12_bytes")
{
    EXPECT_EQ(sizeof(MsgHeader), 12u);
}

TEST("protocol/HelloPayload_layout")
{
    // HwidHash[32] + BuildId(4) + LoaderHash[32] = 68 bytes minimum
    EXPECT(sizeof(HelloPayload) >= 68u);
    // No unexpected padding (it's #pragma pack(push,1))
    EXPECT_EQ(sizeof(HelloPayload), 68u);
}

TEST("protocol/AuthOkPayload_layout")
{
    // SessionToken[16] + ExpiresAt(4) = 20 bytes
    EXPECT_EQ(sizeof(AuthOkPayload), 20u);
}

TEST("protocol/BinaryReadyPayload_layout")
{
    // 4 × uint32_t = 16 bytes
    EXPECT_EQ(sizeof(BinaryReadyPayload), 16u);
}

TEST("protocol/HeartbeatPayload_layout")
{
    // Nonce[16]
    EXPECT_EQ(sizeof(HeartbeatPayload), 16u);
}

TEST("protocol/magic_is_SPKY")
{
    // "SPKY" in little-endian is 0x53504B59
    EXPECT_EQ(PROTO_MAGIC, 0x53504B59u);
}

TEST("protocol/version_is_3")
{
    EXPECT_EQ(PROTO_VERSION, (uint8_t)3);
}

// =============================================================================
// End-to-end message frame encode/decode round-trip
// (exercises Crc32 + XorStream together the way SendMsg/RecvMsg use them)
// =============================================================================

TEST("protocol/frame_encrypt_decrypt_roundtrip")
{
    const uint8_t token[16] = {0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88,
                                0x99,0xAA,0xBB,0xCC,0xDD,0xEE,0xFF,0x00};
    const uint64_t hdrKey = DeriveKey(token, 0);

    // Build a fake payload
    const uint8_t original[] = {0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08};
    const uint32_t len = sizeof(original);

    // Simulate SendMsg: build header, encrypt payload, compute CRC
    MsgHeader hdr{};
    hdr.Magic   = PROTO_MAGIC;
    hdr.Version = PROTO_VERSION;
    hdr.Type    = MsgType::Config;
    hdr.Length  = (uint16_t)len;

    uint8_t encrypted[sizeof(original)];
    memcpy(encrypted, original, len);
    XorStream(encrypted, len, hdrKey);

    uint32_t sentCrc = Crc32((uint8_t*)&hdr, sizeof(hdr))
                     ^ Crc32(encrypted, len);

    // Simulate RecvMsg: verify CRC, decrypt
    uint32_t checkCrc = Crc32((uint8_t*)&hdr, sizeof(hdr))
                      ^ Crc32(encrypted, len);
    EXPECT_EQ(sentCrc, checkCrc); // CRC must match

    uint8_t decrypted[sizeof(original)];
    memcpy(decrypted, encrypted, len);
    XorStream(decrypted, len, hdrKey);

    EXPECT_EQ(memcmp(decrypted, original, len), 0); // must round-trip
}

TEST("protocol/frame_crc_detects_corruption")
{
    const uint8_t token[16] = {0xCA,0xFE,0xBA,0xBE,0,0,0,0,0,0,0,0,0,0,0,0};
    const uint64_t hdrKey = DeriveKey(token, 0);

    MsgHeader hdr{};
    hdr.Magic   = PROTO_MAGIC;
    hdr.Version = PROTO_VERSION;
    hdr.Type    = MsgType::Ack;
    hdr.Length  = 4;

    uint8_t payload[] = {0xDE, 0xAD, 0xBE, 0xEF};
    XorStream(payload, 4, hdrKey);

    uint32_t goodCrc = Crc32((uint8_t*)&hdr, sizeof(hdr)) ^ Crc32(payload, 4);

    // Flip one bit in the payload
    payload[2] ^= 0x01;

    uint32_t badCrc = Crc32((uint8_t*)&hdr, sizeof(hdr)) ^ Crc32(payload, 4);
    EXPECT_NE(goodCrc, badCrc); // corruption must be detected
}
