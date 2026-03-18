cmake_minimum_required(VERSION 3.20...3.28)
project(SparkyServer VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin")

add_executable(SparkyServer src/main.cpp)

target_include_directories(SparkyServer PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../SparkyLoader/include
)

target_link_libraries(SparkyServer PRIVATE ws2_32)

target_compile_definitions(SparkyServer PRIVATE
    WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS
)

if(MSVC)
    target_compile_options(SparkyServer PRIVATE /W3 /utf-8)
else()
    target_compile_options(SparkyServer PRIVATE -Wall -Wextra)
endif()
