#pragma once
// ---------------------------------------------------------------------------
// LicenseManager — license key generation + activation helpers
// Key format: XXXX-XXXX-XXXX-XXXX  (Base32-ish: A-Z 0-9 minus O/I/0/1)
// ---------------------------------------------------------------------------
#include <string>
#include <cstdint>

class Database;

class LicenseManager
{
public:
    explicit LicenseManager(Database& db) : m_db(db) {}

    // Generate a new license key (does NOT insert into DB).
    // Uses CryptGenRandom / RtlGenRandom on Windows.
    static std::string GenerateKey();

    // Issue a new license: generates a key, inserts into DB, returns the key.
    // tier: 1=basic, 2=premium
    // lifetime_days: 0 = perpetual
    std::string IssueLicense(int tier, int lifetime_days, const std::string& note = "");

    // Activate a license for a HWID (bind + create user row).
    // Returns "" on success, error string on failure.
    std::string ActivateLicense(const std::string& key,
                                const std::string& hwid_hash,
                                int64_t            now);

    // Revoke a license (set expires_at to 0 in the past).
    bool RevokeLicense(const std::string& key);

private:
    Database& m_db;
};
