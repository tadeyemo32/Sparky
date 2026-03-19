cmake_minimum_required(VERSION 3.20...3.28)
project(SparkyLoader VERSION 0.2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD          20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS        OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin")

# ---------------------------------------------------------------------------
# OpenSSL — for TLS connection to SparkyServer.
#
# Windows:  vcpkg install openssl:x64-windows  OR set OPENSSL_ROOT_DIR.
# Linux:    apt install libssl-dev
#
# The loader attempts TLS on every connect. If the server is running in
# plaintext dev-mode (no cert/key loaded), connect will fail — ensure
# both sides are in the same mode.
# ---------------------------------------------------------------------------
find_package(OpenSSL REQUIRED)
message(STATUS "SparkyLoader: OpenSSL ${OPENSSL_VERSION} found")

# ---------------------------------------------------------------------------
# GLFW (submodule)
# ---------------------------------------------------------------------------
set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL        OFF CACHE BOOL "" FORCE)
add_subdirectory(${CMAKE_SOURCE_DIR}/../deps/glfw deps/glfw_build)

# ---------------------------------------------------------------------------
# ImGui (submodule — compile sources directly)
# ---------------------------------------------------------------------------
set(IMGUI_DIR ${CMAKE_SOURCE_DIR}/../deps/imgui)

add_library(imgui STATIC
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
    ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui PUBLIC
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
)
target_link_libraries(imgui PUBLIC glfw)

# ---------------------------------------------------------------------------
# SparkyLoader executable
# ---------------------------------------------------------------------------
add_executable(SparkyLoader WIN32
    src/main.cpp
    src/Logger.cpp
    src/ManualMap.cpp
    src/UI.cpp
)

target_include_directories(SparkyLoader PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
)

target_link_libraries(SparkyLoader PRIVATE
    imgui
    glfw
    psapi
    opengl32
    OpenSSL::SSL
    OpenSSL::Crypto
    crypt32
    ws2_32
)

target_compile_definitions(SparkyLoader PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _CRT_SECURE_NO_WARNINGS
    UNICODE
    _UNICODE
)

if(MSVC)
    target_compile_options(SparkyLoader PRIVATE /W3 /utf-8)
else()
    target_compile_options(SparkyLoader PRIVATE -Wall -Wextra)
    target_link_options(SparkyLoader PRIVATE -municode -mwindows)
endif()

target_compile_options(SparkyLoader PRIVATE
    $<$<CONFIG:Debug>:$<IF:$<CXX_COMPILER_ID:MSVC>,/Od /Zi,-g -O0>>
    $<$<CONFIG:Release>:$<IF:$<CXX_COMPILER_ID:MSVC>,/O2,-O2 -DNDEBUG>>
)

add_custom_command(TARGET SparkyLoader POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E echo "SparkyLoader -> bin/SparkyLoader.exe"
)

# ---------------------------------------------------------------------------
# SparkyTests — standalone unit-test executable.
#
# Deliberately avoids OpenSSL, ImGui, and GLFW so it can be built and run
# without a full GUI / TLS setup.  Uses the same ManualMap.cpp and Logger.cpp
# as the main loader, but links only the minimal Win32 libraries they need.
#
# Build:  cmake --build . --target SparkyTests
# Run:    bin/SparkyTests.exe        (returns 0 on all-pass, 1 on any failure)
# ---------------------------------------------------------------------------
add_executable(SparkyTests
    tests/unit/test_protocol.cpp
    tests/unit/test_manualmap_validation.cpp
    src/Logger.cpp
    src/ManualMap.cpp
)

# main() is generated into the build directory so we don't need a committed file.
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/tests_main.cpp"
    "#include \"TestRunner.h\"\nint main() { return TestRunner::Run(); }\n")
target_sources(SparkyTests PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/tests_main.cpp")

target_include_directories(SparkyTests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/unit
)

target_link_libraries(SparkyTests PRIVATE
    psapi
    advapi32
    ws2_32
)

target_compile_definitions(SparkyTests PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _CRT_SECURE_NO_WARNINGS
    UNICODE
    _UNICODE
)

if(MSVC)
    target_compile_options(SparkyTests PRIVATE /W3 /utf-8)
else()
    target_compile_options(SparkyTests PRIVATE -Wall -Wextra)
endif()

target_compile_options(SparkyTests PRIVATE
    $<$<CONFIG:Debug>:$<IF:$<CXX_COMPILER_ID:MSVC>,/Od /Zi,-g -O0>>
    $<$<CONFIG:Release>:$<IF:$<CXX_COMPILER_ID:MSVC>,/O2,-O2 -DNDEBUG>>
)

add_custom_command(TARGET SparkyTests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E echo "SparkyTests -> bin/SparkyTests.exe"
)

# ---------------------------------------------------------------------------
# AntiCheatSim — hardened target process for integration testing.
#
# Runs four watchdog threads that mimic EAC/BattlEye detection engines:
#   1. Thread start-address watchdog (NtQueryInformationThread)
#   2. Page scanner (MZ headers in private memory)
#   3. .text integrity guard (CRC-32 checksum)
#   4. String signature scanner (known payload strings)
#
# Reports detections to stdout and waits for "Global\SparkyDllSuccess".
# Exit code 0 = DLL signalled success; 1 = timeout.
# ---------------------------------------------------------------------------
add_executable(AntiCheatSim
    tests/AntiCheatSim.cpp
)

target_include_directories(AntiCheatSim PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(AntiCheatSim PRIVATE
    psapi
    advapi32
)

target_compile_definitions(AntiCheatSim PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _CRT_SECURE_NO_WARNINGS
    UNICODE
    _UNICODE
)

if(MSVC)
    target_compile_options(AntiCheatSim PRIVATE /W3 /utf-8)
else()
    target_compile_options(AntiCheatSim PRIVATE -Wall -Wextra)
endif()

target_compile_options(AntiCheatSim PRIVATE
    $<$<CONFIG:Debug>:$<IF:$<CXX_COMPILER_ID:MSVC>,/Od /Zi,-g -O0>>
    $<$<CONFIG:Release>:$<IF:$<CXX_COMPILER_ID:MSVC>,/O2,-O2 -DNDEBUG>>
)

# ---------------------------------------------------------------------------
# InjectDirect — headless CLI injector used by the integration test.
#
# Usage: InjectDirect.exe <pid> <dll_path> [--no-erase] [--no-dllmain]
# Exit:  0 = success, 1 = failure, 2 = bad args
# ---------------------------------------------------------------------------
add_executable(InjectDirect
    tests/InjectDirect.cpp
    src/Logger.cpp
    src/ManualMap.cpp
)

target_include_directories(InjectDirect PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(InjectDirect PRIVATE
    psapi
    advapi32
    ws2_32
)

target_compile_definitions(InjectDirect PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _CRT_SECURE_NO_WARNINGS
    UNICODE
    _UNICODE
)

if(MSVC)
    target_compile_options(InjectDirect PRIVATE /W3 /utf-8)
    target_link_options(InjectDirect PRIVATE /SUBSYSTEM:CONSOLE)
else()
    target_compile_options(InjectDirect PRIVATE -Wall -Wextra)
    target_link_options(InjectDirect PRIVATE -mconsole)
endif()

target_compile_options(InjectDirect PRIVATE
    $<$<CONFIG:Debug>:$<IF:$<CXX_COMPILER_ID:MSVC>,/Od /Zi,-g -O0>>
    $<$<CONFIG:Release>:$<IF:$<CXX_COMPILER_ID:MSVC>,/O2,-O2 -DNDEBUG>>
)

# ---------------------------------------------------------------------------
# DummyDll — injected payload for integration testing.
#
# On DLL_PROCESS_ATTACH it sets the named event "Global\SparkyDllSuccess"
# so IntegrationTest.py and AntiCheatSim.exe can confirm silent injection.
# No MessageBox, no visible side effects.
# ---------------------------------------------------------------------------
add_library(DummyDll SHARED
    tests/DummyDll.cpp
)

target_compile_definitions(DummyDll PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _CRT_SECURE_NO_WARNINGS
)

if(MSVC)
    target_compile_options(DummyDll PRIVATE /W3 /utf-8)
endif()

# Convenience target that builds the entire integration test suite at once.
add_custom_target(IntegrationSuite
    DEPENDS AntiCheatSim InjectDirect DummyDll SparkyTests
    COMMENT "Building all Sparky test binaries..."
)
