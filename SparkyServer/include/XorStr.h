#pragma once

#include <string>
#include <cstdint>

// ---------------------------------------------------------------------------
// XorStr — compile-time XOR string encryption with position-dependent keys.
//
// Each byte is encrypted with a key derived from three constants and its
// position, so adjacent bytes have different keys.  A single-byte XOR scan
// of the binary cannot reconstruct strings without knowing the key schedule.
//
// Usage:  XS("some string")  — returns const char* backed by static storage.
// ---------------------------------------------------------------------------

template<size_t N>
struct XorStr
{
    char m_buf[N]{};

    // Per-position key: mixes three compile-time constants with the byte index.
    // All arithmetic on uint8_t wraps silently (well-defined for unsigned types).
    static constexpr uint8_t ChKey(size_t i) noexcept
    {
        constexpr uint8_t A = 0x5A, B = 0xA3, C = 0x71;
        return static_cast<uint8_t>(
            (A + static_cast<uint8_t>(B * static_cast<uint8_t>(i))) ^
            (C >> ((i & 3u) << 1u))                                  ^
            static_cast<uint8_t>(i >> 3u)
        );
    }

    consteval XorStr(const char (&str)[N])
    {
        for (size_t i = 0; i < N; i++)
            m_buf[i] = str[i] ^ static_cast<char>(ChKey(i));
    }

    [[nodiscard]] std::string Decrypt() const
    {
        std::string out(N - 1, '\0');
        for (size_t i = 0; i < N - 1; i++)
            out[i] = static_cast<char>(static_cast<uint8_t>(m_buf[i]) ^ ChKey(i));
        return out;
    }
};

// Compile-time XOR string encryption.
// The string is stored encrypted in the binary and decrypted on first use.
// Returns const char* backed by static storage — safe for any context.
#define XS(str)                                                         \
    ([]() -> const char*                                                \
    {                                                                   \
        static constexpr XorStr<sizeof(str)> _xs(str);                 \
        static const std::string _s = _xs.Decrypt();                   \
        return _s.c_str();                                              \
    }())
