cmake_minimum_required(VERSION 3.20...3.28)
project(SparkyServer VERSION 3.2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin")

# ---------------------------------------------------------------------------
# PostgreSQL (libpq) — must be installed on the build machine.
#
# Windows:  install PostgreSQL from postgresql.org; CMake finds it via
#           the POSTGRESQL_ROOT hint or the registry.
# Linux:    apt install libpq-dev  /  dnf install libpq-devel
#
# Connection string is loaded at runtime from:
#   SPARKY_PG_CONNSTR env var   — full libpq connection string
#   SPARKY_PG_CONNFILE env var  — path to a file containing the string
#   sparky.connstr              — file next to the binary (chmod 400)
#
# Example connection string:
#   host=localhost port=5432 dbname=sparky user=sparky password=s3cr3t sslmode=require
# ---------------------------------------------------------------------------
find_package(Threads REQUIRED)
find_package(PostgreSQL REQUIRED)
message(STATUS "SparkyServer: PostgreSQL ${PostgreSQL_VERSION_STRING} found")

# ---------------------------------------------------------------------------
# OpenSSL — optional at build time but enabled by default.
#
# Windows:  install via vcpkg (vcpkg install openssl:x64-windows) or
#           from slproweb.com; point OPENSSL_ROOT_DIR at the install prefix.
# Linux:    apt install libssl-dev  /  dnf install openssl-devel
#
# TLS is optional at *runtime*: place sparky.crt + sparky.key next to the
# binary to enable TLS; if absent the server falls back to plaintext (dev mode).
# ---------------------------------------------------------------------------
find_package(OpenSSL REQUIRED)
message(STATUS "SparkyServer: OpenSSL ${OPENSSL_VERSION} found")

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
    ${CMAKE_CURRENT_SOURCE_DIR}/../SparkyLoader/user/include
    ${PostgreSQL_INCLUDE_DIRS}
)

target_link_libraries(SparkyServer PRIVATE
    PostgreSQL::PostgreSQL
    OpenSSL::SSL
    OpenSSL::Crypto
    Threads::Threads
)

if(WIN32)
    target_link_libraries(SparkyServer PRIVATE ws2_32 advapi32 crypt32)
endif()

target_compile_definitions(SparkyServer PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _CRT_SECURE_NO_WARNINGS
)

if(MSVC)
    target_compile_options(SparkyServer PRIVATE /W3 /utf-8)
else()
    target_compile_options(SparkyServer PRIVATE -Wall -Wextra)
endif()

# ---------------------------------------------------------------------------
# Tests — no external framework; pure C++20 with socket-pair integration tests.
# Build: cmake --build . --target SparkyTests
# Run:   ./bin/SparkyTests
# ---------------------------------------------------------------------------
add_executable(SparkyTests test/test_main.cpp)
target_include_directories(SparkyTests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../SparkyLoader/user/include
)
target_link_libraries(SparkyTests PRIVATE
    OpenSSL::SSL
    OpenSSL::Crypto
    Threads::Threads
)
if(WIN32)
    target_link_libraries(SparkyTests PRIVATE ws2_32)
endif()
if(MSVC)
    target_compile_options(SparkyTests PRIVATE /W3 /utf-8)
else()
    target_compile_options(SparkyTests PRIVATE -Wall -Wextra)
endif()

enable_testing()
add_test(NAME SparkyTests COMMAND SparkyTests)

# ---------------------------------------------------------------------------
# Benchmarks — run standalone; accepts optional filter args.
# Build: cmake --build . --target SparkyBench
# Run:   ./bin/SparkyBench [xorstream|crc32|crypto|protocol|streaming|connrate|all]
# ---------------------------------------------------------------------------
add_executable(SparkyBench test/bench_main.cpp)
target_include_directories(SparkyBench PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../SparkyLoader/user/include
)
target_link_libraries(SparkyBench PRIVATE
    OpenSSL::SSL
    OpenSSL::Crypto
    Threads::Threads
)
if(WIN32)
    target_link_libraries(SparkyBench PRIVATE ws2_32)
endif()
if(MSVC)
    target_compile_options(SparkyBench PRIVATE /W3 /utf-8)
else()
    target_compile_options(SparkyBench PRIVATE -Wall -Wextra -O3)
endif()
