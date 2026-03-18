cmake_minimum_required(VERSION 3.20...3.28)
project(SparkyServer VERSION 3.0 LANGUAGES CXX)

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
find_package(PostgreSQL REQUIRED)
message(STATUS "SparkyServer: PostgreSQL ${PostgreSQL_VERSION_STRING} found")

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
    ${PostgreSQL_INCLUDE_DIRS}
)

target_link_libraries(SparkyServer PRIVATE
    PostgreSQL::PostgreSQL
    ws2_32
    advapi32
)

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
