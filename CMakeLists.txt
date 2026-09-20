cmake_minimum_required(VERSION 3.20)
project(VCB CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Force Release build type unless the caller explicitly overrides.
if (NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

set(VCB_CORE_SOURCES
    src/analysis.cpp
    src/optimization.cpp
    src/lowering.cpp
    src/target.cpp
    src/codegen.cpp
    src/parser.cpp
)

# --- vcb (main executable) ---
add_executable(vcb src/main.cpp ${VCB_CORE_SOURCES})
target_include_directories(vcb PRIVATE include)

# --- vcb-tests ---
add_executable(vcb-tests test/tests.cpp ${VCB_CORE_SOURCES})
target_include_directories(vcb-tests PRIVATE include)

# --- Compiler-specific flags ---
if (MSVC)
    foreach (tgt vcb vcb-tests)
        target_compile_options(${tgt} PRIVATE
            /W4
            $<$<CONFIG:Release>:/O2 /GL /fp:fast>
            $<$<CONFIG:Release>:/MT>
        )
        target_link_options(${tgt} PRIVATE
            $<$<CONFIG:Release>:/LTCG>
        )
    endforeach()
else()
    foreach (tgt vcb vcb-tests)
        target_compile_options(${tgt} PRIVATE
            -O2 -Wall -Wextra
            $<$<CONFIG:Release>:-static>
        )
    endforeach()
endif()