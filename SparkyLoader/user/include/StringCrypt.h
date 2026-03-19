#pragma once
// ---------------------------------------------------------------------------
// StringCrypt.h — compile-time XOR string obfuscation.
//
// Sensitive string literals (DLL names, Nt function names, etc.) must not
// appear as plaintext in the binary's .rdata section.  AV engines and
// commercial anti-cheats grep compiled binaries for strings like "ntdll.dll",
// "NtAllocateVirtualMemory", and "GetProcAddress" to build loader signatures.
//
// This header provides two template classes and convenience macros that
// encrypt string literals at compile time using a per-call-site rotating XOR
// key, then decrypt them on demand at runtime into a stack-local buffer.
// The plaintext only exists momentarily in CPU registers/stack while the API
// call that consumes it is in progress; it is never stored in any persistent
// section of the binary.
//
// ── Usage ─────────────────────────────────────────────────────────────────
//
//   Named objects (safe to call c_str() multiple times):
//       SPARKY_STR(var, "ntdll.dll");
//       HMODULE h = GetModuleHandleA(var.c_str());
//
//       SPARKY_WSTR(wvar, L"advapi32.dll");
//       HMODULE h = GetModuleHandleW(wvar.c_str());
//
//   Inline / single-expression (temporary; safe only when the pointer is
//   consumed within the same full-expression as the macro, i.e. passed
//   directly to a function call):
//       HMODULE h = GetModuleHandleA(_S("ntdll.dll"));
//       HMODULE h = GetModuleHandleW(_SW(L"ntdll.dll"));
//
//   ⚠  Never store the result of _S() / _SW() in a pointer variable.
//      The temporary SparkyStr / SparkyWStr is destroyed at the semicolon.
//
// ── Key derivation ────────────────────────────────────────────────────────
//
//   Key = (((__LINE__ * 0x9EU) + (__COUNTER__ * 0x6BU)) & 0xFF)
//
//   __COUNTER__ advances globally across the TU, so two uses of _S() on the
//   same line still get different keys.  The per-byte rotation below further
//   ensures that a shared first byte doesn't leak the key to a pattern search.
//
// ---------------------------------------------------------------------------

#include <cstdint>
#include <cstddef>

// ---------------------------------------------------------------------------
// SparkyStr<N, Key> — narrow (char) encrypted string
// ---------------------------------------------------------------------------
template<size_t N, uint8_t Key>
class SparkyStr
{
public:
    // Encrypt at compile time.
    constexpr explicit SparkyStr(const char (&s)[N]) noexcept
    {
        for (size_t i = 0; i < N; ++i)
            _enc[i] = static_cast<char>(static_cast<uint8_t>(s[i]) ^ _ks(i));
    }

    // Decrypt into internal buffer and return a pointer to it.
    // The returned pointer is valid for the lifetime of this object.
    // Multiple calls are idempotent (each call decrypts from _enc fresh).
    [[nodiscard]] const char* c_str() const noexcept
    {
        for (size_t i = 0; i < N; ++i)
            _dec[i] = static_cast<char>(static_cast<uint8_t>(_enc[i]) ^ _ks(i));
        return _dec;
    }

    // Scrub both buffers when the object is destroyed so plaintext
    // doesn't linger in freed stack frames.
    ~SparkyStr() noexcept
    {
        for (size_t i = 0; i < N; ++i)
        {
            volatile char* ep = const_cast<volatile char*>(&_enc[i]);
            volatile char* dp = const_cast<volatile char*>(&_dec[i]);
            *ep = 0; *dp = 0;
        }
    }

    // Non-copyable — stack lifetime only.
    SparkyStr(const SparkyStr&)            = delete;
    SparkyStr& operator=(const SparkyStr&) = delete;

private:
    // Per-byte key schedule: rotating addition + index folding.
    // Using a non-linear schedule means equal plaintext bytes encrypt to
    // different cipher bytes, preventing repeat-byte pattern attacks.
    static constexpr uint8_t _ks(size_t i) noexcept
    {
        return static_cast<uint8_t>(Key + static_cast<uint8_t>(i * 7u))
             ^ static_cast<uint8_t>(i >> 3);
    }

    char         _enc[N]{};           // compile-time encrypted (.rdata or .data)
    mutable char _dec[N]{};           // runtime plaintext buffer (stack / .bss)
};

// ---------------------------------------------------------------------------
// SparkyWStr<N, Key> — wide (wchar_t) encrypted string
// ---------------------------------------------------------------------------
template<size_t N, uint8_t Key>
class SparkyWStr
{
public:
    constexpr explicit SparkyWStr(const wchar_t (&s)[N]) noexcept
    {
        for (size_t i = 0; i < N; ++i)
            _enc[i] = static_cast<wchar_t>(s[i] ^ static_cast<wchar_t>(_ks(i)));
    }

    [[nodiscard]] const wchar_t* c_str() const noexcept
    {
        for (size_t i = 0; i < N; ++i)
            _dec[i] = static_cast<wchar_t>(_enc[i] ^ static_cast<wchar_t>(_ks(i)));
        return _dec;
    }

    ~SparkyWStr() noexcept
    {
        for (size_t i = 0; i < N; ++i)
        {
            volatile wchar_t* ep = const_cast<volatile wchar_t*>(&_enc[i]);
            volatile wchar_t* dp = const_cast<volatile wchar_t*>(&_dec[i]);
            *ep = 0; *dp = 0;
        }
    }

    SparkyWStr(const SparkyWStr&)            = delete;
    SparkyWStr& operator=(const SparkyWStr&) = delete;

private:
    static constexpr uint8_t _ks(size_t i) noexcept
    {
        return static_cast<uint8_t>(Key + static_cast<uint8_t>(i * 7u))
             ^ static_cast<uint8_t>(i >> 3);
    }

    wchar_t         _enc[N]{};
    mutable wchar_t _dec[N]{};
};

// ---------------------------------------------------------------------------
// Key derivation macro
// __LINE__ and __COUNTER__ are mixed with prime multipliers so that two
// identical string literals at different call sites use different keys.
// ---------------------------------------------------------------------------
#define _SPARKY_KEY \
    static_cast<uint8_t>(((__LINE__ * 0x9EU) + (__COUNTER__ * 0x6BU)) & 0xFFU)

// ---------------------------------------------------------------------------
// Named-object macros (preferred — clearly-scoped lifetime)
// ---------------------------------------------------------------------------

// SPARKY_STR(varname, "narrow string")
//   Creates a SparkyStr named `varname` on the stack.
//   Use varname.c_str() to pass to Win32 APIs.
#define SPARKY_STR(var, s) \
    SparkyStr<sizeof(s), _SPARKY_KEY> var(s)

// SPARKY_WSTR(varname, L"wide string")
//   Wide-char version.
#define SPARKY_WSTR(var, s) \
    SparkyWStr<(sizeof(s) / sizeof(wchar_t)), _SPARKY_KEY> var(s)

// ---------------------------------------------------------------------------
// Inline / temporary macros (use ONLY as a direct function argument)
// ---------------------------------------------------------------------------

// _S("string") — decrypts inline; ONLY safe when used as a direct argument
//   to a function that copies the string before the statement ends, e.g.:
//     HMODULE h = GetModuleHandleA(_S("ntdll.dll"));  // OK
//     const char* p = _S("ntdll.dll");                // UNDEFINED BEHAVIOUR
#define _S(s) \
    (SparkyStr<sizeof(s), _SPARKY_KEY>(s).c_str())

// _SW(L"string") — wide version of _S().
#define _SW(s) \
    (SparkyWStr<(sizeof(s) / sizeof(wchar_t)), _SPARKY_KEY>(s).c_str())
