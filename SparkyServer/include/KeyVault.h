#pragma once
// ---------------------------------------------------------------------------
// KeyVault — secure master-key loading for SQLCipher database.
//
// Resolution order (first that succeeds wins):
//   1. Environment variable  SPARKY_DB_KEY   (64 hex chars = 32 bytes)
//   2. File path in env var  SPARKY_DB_KEYFILE  (file contains hex key)
//   3. File "sparky.key" next to CWD
//
// Key format:  64 lowercase hex characters, no spaces/newlines.
// Example:     SPARKY_DB_KEY=a3f1c8...   (openssl rand -hex 32)
//
// The returned string is formatted as SQLCipher expects:
//   "x'<64 hex chars>'"
// which is passed to:  PRAGMA key = "x'...'"
//
// Security notes:
//   - Never hard-code the key in source.
//   - The key file must be chmod 400, owned by the server user.
//   - On AWS: store the key in Systems Manager Parameter Store (SecureString)
//     and pull it at startup; never write it to disk except in the key file.
//   - Rotate keys with:  PRAGMA rekey = "x'<new key>'"
// ---------------------------------------------------------------------------
#include <string>
#include <cstdlib>
#include <fstream>
#include <cctype>
#include <stdexcept>
#include <iostream>

class KeyVault
{
public:
    // Returns the SQLCipher PRAGMA key string  "x'<hex>'"
    // Throws std::runtime_error if no key can be found.
    static std::string LoadDbKey()
    {
        std::string hex;

        // 1. Env var
        if (const char* env = std::getenv("SPARKY_DB_KEY"))
        {
            hex = env;
        }
        // 2. Path in env var
        else if (const char* kf = std::getenv("SPARKY_DB_KEYFILE"))
        {
            hex = ReadFile(kf);
        }
        // 3. Default key file
        else
        {
            try { hex = ReadFile("sparky.key"); }
            catch (...) {}
        }

        if (hex.empty())
            throw std::runtime_error(
                "No database key found.\n"
                "  Set SPARKY_DB_KEY=<64 hex chars>\n"
                "  or SPARKY_DB_KEYFILE=<path to key file>\n"
                "  or place sparky.key (chmod 400) next to the binary.\n"
                "  Generate a key:  openssl rand -hex 32");

        // Strip whitespace
        std::string clean;
        clean.reserve(64);
        for (char c : hex)
            if (!std::isspace((unsigned char)c)) clean += c;

        if (clean.size() != 64)
            throw std::runtime_error(
                "Database key must be exactly 64 hex characters (32 bytes)");

        for (char c : clean)
            if (!std::isxdigit((unsigned char)c))
                throw std::runtime_error("Database key contains non-hex characters");

        // Lower-case for consistency
        for (char& c : clean) c = (char)std::tolower((unsigned char)c);

        return "x'" + clean + "'";
    }

    // Generate and print a new random key to stdout.
    // Usage:  SparkyServer --gen-key
    static void GenerateKey()
    {
#ifdef _WIN32
        // Windows: use CryptGenRandom
        #include <Windows.h>
        #include <wincrypt.h>
        uint8_t raw[32]{};
        HCRYPTPROV hp{};
        if (CryptAcquireContextW(&hp, nullptr, nullptr, PROV_RSA_AES, CRYPT_VERIFYCONTEXT))
        {
            CryptGenRandom(hp, 32, raw);
            CryptReleaseContext(hp, 0);
        }
        static const char h[] = "0123456789abcdef";
        std::string key;
        for (uint8_t b : raw) { key += h[b>>4]; key += h[b&0xF]; }
        std::cout << key << "\n";
#else
        // Linux: read /dev/urandom
        std::ifstream f("/dev/urandom", std::ios::binary);
        if (!f.is_open()) { std::cerr << "Cannot open /dev/urandom\n"; return; }
        uint8_t raw[32]{};
        f.read(reinterpret_cast<char*>(raw), 32);
        static const char h[] = "0123456789abcdef";
        std::string key;
        for (uint8_t b : raw) { key += h[b>>4]; key += h[b&0xF]; }
        std::cout << key << "\n";
#endif
    }

private:
    static std::string ReadFile(const char* path)
    {
        std::ifstream f(path);
        if (!f.is_open())
            throw std::runtime_error(std::string("Cannot read key file: ") + path);
        std::string s;
        std::getline(f, s);
        return s;
    }
};
