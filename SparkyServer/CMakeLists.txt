cmake_minimum_required(VERSION 3.20...3.28)
project(SparkyServer VERSION 2.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin")

# ---------------------------------------------------------------------------
# SQLite3 — bundled as a single-file amalgamation.
# Download sqlite-amalgamation from https://www.sqlite.org/download.html
# and place sqlite3.c + sqlite3.h into <repo_root>/deps/sqlite3/
# ---------------------------------------------------------------------------
set(SQLITE3_DIR "${CMAKE_SOURCE_DIR}/../deps/sqlite3")

add_library(sqlite3 STATIC ${SQLITE3_DIR}/sqlite3.c)
target_include_directories(sqlite3 PUBLIC ${SQLITE3_DIR})
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
    ${SQLITE3_DIR}
)

target_link_libraries(SparkyServer PRIVATE
    sqlite3
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
