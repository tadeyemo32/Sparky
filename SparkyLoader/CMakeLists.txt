# CMakeLists.txt

cmake_minimum_required(VERSION 3.20...3.28)
project(SparkyLoader VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD          20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS        OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

add_executable(SparkyLoader
    src/main.cpp
    src/Logger.cpp
    src/ManualMap.cpp
)

target_include_directories(SparkyLoader PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/deps/glad/include
)

target_link_libraries(SparkyLoader PRIVATE
    psapi
)

target_compile_definitions(SparkyLoader PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _CRT_SECURE_NO_WARNINGS
)

if(MSVC)
    target_compile_options(SparkyLoader PRIVATE /W4)
else()  # MinGW
    target_compile_options(SparkyLoader PRIVATE
        -Wall -Wextra -pedantic
    )
endif()

# Debug/Release
target_compile_options(SparkyLoader PRIVATE
    $<$<CONFIG:Debug>:-g -O0>
    $<$<CONFIG:Release>:-O2 -DNDEBUG>
)

# Post-build message
add_custom_command(TARGET SparkyLoader POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E echo "Build done --> bin/SparkyLoader.exe"
)
