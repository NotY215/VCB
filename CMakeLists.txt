cmake_minimum_required(VERSION 3.20)
project(VCB LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

if(MSVC)
    add_compile_options(/W4 /permissive- /Zc:__cplusplus /utf-8 /EHsc)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

add_executable(vcb
    src/main.cpp
    src/ir/Ir.cpp
    src/ir/Parser.cpp
    src/ir/Printer.cpp
    src/x64/X64.cpp
    src/x64/Runtime.cpp
    src/pe/Pe.cpp
    src/driver/Driver.cpp
)

target_include_directories(vcb PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)