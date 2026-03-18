cmake_minimum_required(VERSION 3.20...3.28)
project(SparkyServer VERSION 2.2 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin")

# ---------------------------------------------------------------------------
# SPARKY_SQLCIPHER — set ON for production builds.
#
# OFF (default): uses plain SQLite3 amalgamation from deps/sqlite3/
#                Good for local dev; database is NOT encrypted.
#
# ON:            uses SQLCipher amalgamation from deps/sqlcipher/
#                Requires OpenSSL (or Windows BCrypt — see SQLCipher docs).
#                Database encrypted with AES-256-CBC, PBKDF2-SHA512 key derivation.
#
# SQLCipher download: https://www.zetetic.net/sqlcipher/open-source/
# Place sqlcipher/sqlite3.c and sqlcipher/sqlite3.h into deps/sqlcipher/
# ---------------------------------------------------------------------------
option(SPARKY_SQLCIPHER "Build with SQLCipher AES-256 database encryption" OFF)

if(SPARKY_SQLCIPHER)
    set(DB_DIR "${CMAKE_SOURCE_DIR}/../deps/sqlcipher")
    message(STATUS "SparkyServer: SQLCipher encryption ENABLED")

    add_library(sqlcipher STATIC ${DB_DIR}/sqlite3.c)
    target_include_directories(sqlcipher PUBLIC ${DB_DIR})
    target_compile_definitions(sqlcipher PUBLIC
        SQLITE_HAS_CODEC
        SQLITE_THREADSAFE=1
        SQLITE_DEFAULT_WAL_AUTOCHECKPOINT=1000
        SQLCIPHER_CRYPTO_OPENSSL          # or SQLCIPHER_CRYPTO_WINCNG on Windows
        _CRT_SECURE_NO_WARNINGS
    )
    if(MSVC)
        target_compile_options(sqlcipher PRIVATE /W0)
        # Link against OpenSSL or Windows BCrypt
        target_link_libraries(sqlcipher PUBLIC bcrypt)
    else()
        target_compile_options(sqlcipher PRIVATE -w)
        find_package(OpenSSL REQUIRED)
        target_link_libraries(sqlcipher PUBLIC OpenSSL::Crypto)
    endif()

    set(DB_LIB sqlcipher)
    set(DB_CIPHER_DEF SPARKY_SQLCIPHER)
else()
    set(DB_DIR "${CMAKE_SOURCE_DIR}/../deps/sqlite3")
    message(STATUS "SparkyServer: plain SQLite (dev mode — no encryption)")

    add_library(sqlite3 STATIC ${DB_DIR}/sqlite3.c)
    target_include_directories(sqlite3 PUBLIC ${DB_DIR})
    target_compile_definitions(sqlite3 PRIVATE
        SQLITE_THREADSAFE=1
        SQLITE_DEFAULT_WAL_AUTOCHECKPOINT=1000
        SQLITE_ENABLE_FTS5
        _CRT_SECURE_NO_WARNINGS
    )
    if(MSVC)
        target_compile_options(sqlite3 PRIVATE /W0)
    else()
        target_compile_options(sqlite3 PRIVATE -w)
    endif()

    set(DB_LIB sqlite3)
    set(DB_CIPHER_DEF "")
endif()

# ---------------------------------------------------------------------------
# SparkyServer
# ---------------------------------------------------------------------------
add_executable(SparkyServer
    src/main.cpp
    src/Database.cpp
    src/LicenseManager.cpp
)

target_include_directories(SparkyServer PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../SparkyLoader/include
    ${DB_DIR}
)

target_link_libraries(SparkyServer PRIVATE
    ${DB_LIB}
    ws2_32
    advapi32
)

target_compile_definitions(SparkyServer PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _CRT_SECURE_NO_WARNINGS
    ${DB_CIPHER_DEF}
)

if(MSVC)
    target_compile_options(SparkyServer PRIVATE /W3 /utf-8)
else()
    target_compile_options(SparkyServer PRIVATE -Wall -Wextra)
endif()
