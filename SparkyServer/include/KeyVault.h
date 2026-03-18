#pragma once
// ---------------------------------------------------------------------------
// KeyVault — PostgreSQL connection string loader.
//
// Resolution order (first that succeeds wins):
//   1. Environment variable  SPARKY_PG_CONNSTR   (full libpq connection string)
//   2. File path in env var  SPARKY_PG_CONNFILE   (file contains the string)
//   3. File "sparky.connstr" next to CWD
//
// Connection string format (libpq keyword=value):
//   host=localhost port=5432 dbname=sparky user=sparky password=s3cr3t sslmode=require
//
// Or URI form:
//   postgresql://sparky:s3cr3t@localhost:5432/sparky?sslmode=require
//
// Security notes:
//   - Never hard-code credentials in source.
//   - The connstr file must be chmod 400, owned by the server user.
//   - Use sslmode=require (or verify-full) in production.
//   - On cloud: store in AWS SSM Parameter Store / GCP Secret Manager and
//     inject at startup via the environment variable.
// ---------------------------------------------------------------------------
#include <string>
#include <cstdlib>
#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <iostream>

#ifdef _WIN32
#include <Windows.h>
#include <wincrypt.h>
#pragma comment(lib, "advapi32.lib")
#endif

class KeyVault
{
public:
    // Returns the libpq connection string.
    // Throws std::runtime_error if no connection string can be found.
    static std::string LoadConnStr()
    {
        std::string connstr;

        // 1. Env var — full connection string
        if (const char* env = std::getenv("SPARKY_PG_CONNSTR"))
        {
            connstr = env;
        }
        // 2. Env var — path to file containing the connection string
        else if (const char* kf = std::getenv("SPARKY_PG_CONNFILE"))
        {
            connstr = ReadFile(kf);
        }
        // 3. Default file next to binary
        else
        {
            try { connstr = ReadFile("sparky.connstr"); }
            catch (...) {}
        }

        if (connstr.empty())
            throw std::runtime_error(
                "No PostgreSQL connection string found.\n"
                "  Set SPARKY_PG_CONNSTR='host=... port=5432 dbname=sparky user=sparky password=...'\n"
                "  or SPARKY_PG_CONNFILE=<path to connstr file>\n"
                "  or place sparky.connstr (chmod 400) next to the binary.\n"
                "  TIP: use sslmode=require in production.");

        // Strip trailing whitespace / newlines
        while (!connstr.empty()
               && (connstr.back() == '\n' || connstr.back() == '\r'
                   || connstr.back() == ' '))
            connstr.pop_back();

        return connstr;
    }

    // Generate and print a new 32-byte random hex token to stdout.
    // Useful for generating application secrets (not DB passwords).
    static void GenerateToken()
    {
        uint8_t raw[32]{};

#ifdef _WIN32
        HCRYPTPROV hp{};
        if (!CryptAcquireContextW(&hp, nullptr, nullptr, PROV_RSA_AES, CRYPT_VERIFYCONTEXT)
            || !CryptGenRandom(hp, 32, raw))
        {
            std::cerr << "CryptGenRandom failed\n"; return;
        }
        CryptReleaseContext(hp, 0);
#else
        std::ifstream f("/dev/urandom", std::ios::binary);
        if (!f.is_open()) { std::cerr << "Cannot open /dev/urandom\n"; return; }
        f.read(reinterpret_cast<char*>(raw), 32);
        if (!f) { std::cerr << "/dev/urandom read failed\n"; return; }
#endif

        static const char h[] = "0123456789abcdef";
        std::string token;
        token.reserve(64);
        for (uint8_t b : raw) { token += h[b >> 4]; token += h[b & 0xF]; }
        std::cout << token << "\n";
    }

private:
    static std::string ReadFile(const char* path)
    {
        std::ifstream f(path);
        if (!f.is_open())
            throw std::runtime_error(std::string("Cannot read file: ") + path);
        std::string s;
        std::getline(f, s);
        return s;
    }
};
