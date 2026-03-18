cmake_minimum_required(VERSION 3.20...3.28)
project(SparkyCore VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin")

# Shared DLL — injected into the target by SparkyLoader
add_library(SparkyCore SHARED
    src/DllMain.cpp
)

target_include_directories(SparkyCore PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_definitions(SparkyCore PRIVATE
    WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS
)

if(MSVC)
    target_compile_options(SparkyCore PRIVATE /W3 /utf-8)
    # Remove default entry point so DllMain is the real entry
    target_link_options(SparkyCore PRIVATE /ENTRY:DllMain)
else()
    target_compile_options(SparkyCore PRIVATE -Wall -Wextra)
    target_link_options(SparkyCore PRIVATE -shared -e _DllMainCRTStartup@12)
endif()
