cmake_minimum_required(VERSION 3.21)
project(cctxpack VERSION 0.1.0 LANGUAGES C)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

include(CheckCCompilerFlag)

if(MSVC)
    set(CTX_STD_CANDIDATES "/std:c23" "/std:c17")
else()
    set(CTX_STD_CANDIDATES "-std=c23" "-std=c2x" "-std=c17")
endif()

set(CTX_STD_FLAG "")
foreach(flag IN LISTS CTX_STD_CANDIDATES)
    string(MAKE_C_IDENTIFIER "CTX_HAS${flag}" flag_var)
    check_c_compiler_flag("${flag}" ${flag_var})
    if(${flag_var})
        set(CTX_STD_FLAG "${flag}")
        break()
    endif()
endforeach()

if(NOT CTX_STD_FLAG)
    message(FATAL_ERROR "No supported C standard flag found (need at least C17)")
endif()

add_compile_options(${CTX_STD_FLAG})
message(STATUS "cctxpack: C standard flag: ${CTX_STD_FLAG}")

if(MSVC)
    add_compile_options(/W4)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

add_executable(cctxpack src/main.c)
target_compile_definitions(cctxpack PRIVATE CTX_VERSION="${PROJECT_VERSION}")

enable_testing()

add_subdirectory(tests)
