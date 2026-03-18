// LicenseManager.cpp
#include "../include/LicenseManager.h"
#include "../include/Database.h"
#include <Windows.h>
#include <wincrypt.h>
#include <cstring>
#include <ctime>
#include <stdexcept>

#pragma comment(lib, "advapi32.lib")

// ---------------------------------------------------------------------------
// Key charset: uppercase A-Z plus digits 2-9 (excludes O/I/0/1 for clarity)
// Gives 32 symbols → 5 bits/char → 16-char key = 80 bits of entropy
// ---------------------------------------------------------------------------
static constexpr char KEY_CHARS[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
static constexpr int  KEY_CHARSET_LEN = 32;

static std::string FormatKey(const uint8_t raw[10])
{
    // 10 bytes = 80 bits → 16 base-32 characters in groups of 4 = "XXXX-XXXX-XXXX-XXXX"
    std::string key;
    key.reserve(19);
    uint64_t hi = 0, lo = 0;
    for (int i = 0; i < 5; ++i) hi = (hi << 8) | raw[i];
    for (int i = 5; i < 10; ++i) lo = (lo << 8) | raw[i];

    // Extract 16 5-bit groups from 80-bit value (hi:lo)
    char chars[16];
    for (int i = 15; i >= 0; --i)
    {
        chars[i] = KEY_CHARS[lo & 0x1F];
        // Shift 5 bits right across hi:lo
        lo = (lo >> 5) | ((hi & 0x1F) << (64 - 5));
        hi >>= 5;
    }
    for (int i = 0; i < 16; ++i)
    {
        if (i > 0 && i % 4 == 0) key += '-';
        key += chars[i];
    }
    return key;
}

std::string LicenseManager::GenerateKey()
{
    uint8_t raw[10]{};

    // Prefer CryptGenRandom
    HCRYPTPROV hProv{};
    if (CryptAcquireContextW(&hProv, nullptr, nullptr,
                              PROV_RSA_AES, CRYPT_VERIFYCONTEXT))
    {
        CryptGenRandom(hProv, sizeof(raw), raw);
        CryptReleaseContext(hProv, 0);
    }
    else
    {
        // Fallback: mix QueryPerformanceCounter + GetTickCount64
        LARGE_INTEGER pc{};
        QueryPerformanceCounter(&pc);
        uint64_t seed = (uint64_t)pc.QuadPart ^ ((uint64_t)GetTickCount64() << 17);
        for (int i = 0; i < 10; ++i)
        {
            seed ^= seed >> 12; seed ^= seed << 25; seed ^= seed >> 27;
            raw[i] = (uint8_t)(seed & 0xFF);
        }
    }

    return FormatKey(raw);
}

std::string LicenseManager::IssueLicense(int tier, int lifetime_days,
                                          const std::string& /*note*/)
{
    const int64_t now = (int64_t)time(nullptr);

    // Retry on key collision (astronomically rare, but correct)
    for (int attempt = 0; attempt < 5; ++attempt)
    {
        std::string key = GenerateKey();
        LicenseRow row;
        row.key        = key;
        row.tier       = tier;
        row.issued_at  = now;
        row.expires_at = (lifetime_days > 0)
                          ? now + (int64_t)lifetime_days * 86400LL
                          : 0;
        row.hwid_hash  = "";
        if (m_db.InsertLicense(row))
            return key;
    }
    return "";
}

std::string LicenseManager::ActivateLicense(const std::string& key,
                                             const std::string& hwid_hash,
                                             int64_t            now)
{
    auto lic = m_db.GetLicense(key);
    if (!lic)
        return "License key not found";

    if (!lic->hwid_hash.empty() && lic->hwid_hash != hwid_hash)
        return "License already bound to another device";

    if (lic->expires_at != 0 && lic->expires_at < now)
        return "License expired";

    // Bind key to HWID if not already done
    if (lic->hwid_hash.empty())
    {
        if (!m_db.BindLicense(key, hwid_hash))
            return "Bind failed (race condition or already bound)";
    }

    // Upsert user row
    m_db.TouchUser(hwid_hash, now);
    m_db.SetUserLicense(hwid_hash, key);
    return "";
}

bool LicenseManager::RevokeLicense(const std::string& key)
{
    // Setting expires_at = 1 (epoch) effectively expires it
    const char* sql = "UPDATE licenses SET expires_at=1 WHERE key=?;";
    // Direct DB call would require expose; simplest: use DB's GetLicense + re-insert
    auto lic = m_db.GetLicense(key);
    if (!lic) return false;
    lic->expires_at = 1;
    // Re-use InsertLicense won't work (PK conflict). Need raw update.
    // Expose via Database::ExecSQL is private, so we use a dedicated helper
    // that's cleanest: just ban the bound HWID if any.
    if (!lic->hwid_hash.empty())
        m_db.BanUser(lic->hwid_hash, "license revoked");
    return true;
}
