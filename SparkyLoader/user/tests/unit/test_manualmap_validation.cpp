// test_manualmap_validation.cpp
// Unit tests for ManualMapDll() PE header validation paths.
//
// These tests exercise the four early-out checks in ManualMapDll before any
// memory allocation or process-write occurs.  Each test supplies a deliberately
// malformed PE buffer and asserts that ManualMapDll() returns false immediately.
//
// Tests are scoped to the pure validation logic — they never inject code.
#include "TestRunner.h"
#include "ManualMap.h"

#include <vector>
#include <cstring>
#include <cstdint>

// ---------------------------------------------------------------------------
// Helpers — build minimal fake PE buffers
// ---------------------------------------------------------------------------

// DOS stub with e_lfanew = 0x40 (64 bytes), but e_magic is zeroed (invalid).
static std::vector<uint8_t> MakeBadDosBuffer()
{
    // sizeof(IMAGE_DOS_HEADER) == 64; we only need e_magic at offset 0.
    std::vector<uint8_t> buf(128, 0);
    // e_magic left as 0x0000 — not IMAGE_DOS_SIGNATURE (0x5A4D)
    return buf;
}

// Valid DOS header pointing to an NT header with a bad NT signature.
static std::vector<uint8_t> MakeBadNtSigBuffer()
{
    // e_lfanew at offset 0x3C points to offset 0x40.
    const uint32_t ntOffset = 0x40;
    std::vector<uint8_t> buf(ntOffset + sizeof(IMAGE_NT_HEADERS64) + 16, 0);

    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(buf.data());
    dos->e_magic  = IMAGE_DOS_SIGNATURE; // 0x5A4D ('MZ')
    dos->e_lfanew = static_cast<LONG>(ntOffset);

    // NT signature at ntOffset left as 0x00000000 — not IMAGE_NT_SIGNATURE (0x4550)
    return buf;
}

// Valid DOS + NT signature, but Machine field is x86 (0x014C) instead of AMD64 (0x8664).
static std::vector<uint8_t> MakeX86MachineBuffer()
{
    const uint32_t ntOffset = 0x40;
    std::vector<uint8_t> buf(ntOffset + sizeof(IMAGE_NT_HEADERS64) + 16, 0);

    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(buf.data());
    dos->e_magic  = IMAGE_DOS_SIGNATURE;
    dos->e_lfanew = static_cast<LONG>(ntOffset);

    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(buf.data() + ntOffset);
    nt->Signature              = IMAGE_NT_SIGNATURE;  // 0x00004550 ('PE\0\0')
    nt->FileHeader.Machine     = IMAGE_FILE_MACHINE_I386; // 0x014C — must be rejected
    nt->OptionalHeader.Magic   = IMAGE_NT_OPTIONAL_HDR32_MAGIC;
    return buf;
}

// Valid DOS + NT headers with correct AMD64 machine, but SizeOfImage == 0.
// NtAllocateVirtualMemory with zero size must return an error, so ManualMapDll
// returns false at the allocation step (still never writes/executes anything).
static std::vector<uint8_t> MakeAmd64ZeroSizeBuffer()
{
    const uint32_t ntOffset = 0x40;
    std::vector<uint8_t> buf(ntOffset + sizeof(IMAGE_NT_HEADERS64) + 16, 0);

    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(buf.data());
    dos->e_magic  = IMAGE_DOS_SIGNATURE;
    dos->e_lfanew = static_cast<LONG>(ntOffset);

    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(buf.data() + ntOffset);
    nt->Signature              = IMAGE_NT_SIGNATURE;
    nt->FileHeader.Machine     = IMAGE_FILE_MACHINE_AMD64; // 0x8664 — valid
    nt->FileHeader.NumberOfSections = 0;
    nt->OptionalHeader.Magic   = IMAGE_NT_OPTIONAL_HDR64_MAGIC;
    nt->OptionalHeader.SizeOfImage   = 0; // forces allocation to fail
    nt->OptionalHeader.SizeOfHeaders = 0;
    return buf;
}

// =============================================================================
// Tests
// =============================================================================

TEST("manualmap/rejects_empty_buffer")
{
    // ManualMapDll must return false immediately on an empty byte vector.
    std::vector<uint8_t> empty;
    EXPECT_FALSE(ManualMapDll(GetCurrentProcess(), empty, L"", false, false));
}

TEST("manualmap/rejects_bad_dos_magic")
{
    // e_magic != 0x5A4D ('MZ') → bad DOS header → false.
    auto buf = MakeBadDosBuffer();
    EXPECT_FALSE(ManualMapDll(GetCurrentProcess(), buf, L"", false, false));
}

TEST("manualmap/rejects_bad_nt_signature")
{
    // Valid DOS header, but Signature != 0x00004550 ('PE\0\0') → false.
    auto buf = MakeBadNtSigBuffer();
    EXPECT_FALSE(ManualMapDll(GetCurrentProcess(), buf, L"", false, false));
}

TEST("manualmap/rejects_wrong_machine_type")
{
    // Valid DOS + NT sig, but Machine == IMAGE_FILE_MACHINE_I386 → false.
    // Loader must only accept AMD64 (0x8664) DLLs.
    auto buf = MakeX86MachineBuffer();
    EXPECT_FALSE(ManualMapDll(GetCurrentProcess(), buf, L"", false, false));
}

TEST("manualmap/rejects_zero_size_image")
{
    // All header checks pass (AMD64), but SizeOfImage == 0.
    // NtAllocateVirtualMemory must fail → ManualMapDll returns false.
    // No shellcode is executed; GetCurrentProcess() is safe here.
    auto buf = MakeAmd64ZeroSizeBuffer();
    EXPECT_FALSE(ManualMapDll(GetCurrentProcess(), buf, L"", false, false));
}

// ---------------------------------------------------------------------------
// Layout / constant sanity checks (no runtime injection needed)
// ---------------------------------------------------------------------------

TEST("manualmap/MappingData_is_aligned")
{
    // All function pointer fields must sit on 8-byte boundaries.
    // If MappingData grows misaligned the shellcode will read wrong addresses.
    EXPECT_EQ(offsetof(MappingData, LdrLoadDll) % 8, 0u);
    EXPECT_EQ(offsetof(MappingData, LdrGetProcedureAddress) % 8, 0u);
    EXPECT_EQ(offsetof(MappingData, RtlInitUnicodeString) % 8, 0u);
    EXPECT_EQ(offsetof(MappingData, RtlInitAnsiString) % 8, 0u);
    EXPECT_EQ(offsetof(MappingData, RtlAddFunctionTable) % 8, 0u);
    EXPECT_EQ(offsetof(MappingData, hModule) % 8, 0u);
}

TEST("manualmap/MappingData_flags_are_byte_fields")
{
    // CallDllMain and ErasePEHeader must be uint8_t so the shellcode can read
    // them without endian issues.
    EXPECT_EQ(sizeof(MappingData::CallDllMain),   1u);
    EXPECT_EQ(sizeof(MappingData::ErasePEHeader), 1u);
}

TEST("manualmap/IMAGE_FILE_MACHINE_AMD64_value")
{
    // Ensure the constant the loader checks against hasn't been redefined.
    EXPECT_EQ((uint16_t)IMAGE_FILE_MACHINE_AMD64, (uint16_t)0x8664u);
}
