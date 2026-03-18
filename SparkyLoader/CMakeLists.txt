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
