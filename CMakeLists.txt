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
    # C5105 is emitted from the Windows SDK's winbase.h when windows.h is
    # included with a newer MSVC toolset; not actionable in our code.
    add_compile_options(/wd5105)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
    # Strict ISO mode (-std=c23) hides POSIX declarations on glibc (popen/
    # pclose, lstat, dirent d_type, getcwd).  Ask for the default feature set.
    # Inert on Windows and Apple toolchains.
    if(NOT WIN32)
        add_compile_definitions(_DEFAULT_SOURCE)
    endif()
endif()

if(MINGW)
    add_link_options(-static)
endif()

function(ctx_enable_getopt target)
    set(getopt_src ${CMAKE_SOURCE_DIR}/vendor/getopt/getopt_long.c)
    set(getopt_compat ${CMAKE_SOURCE_DIR}/vendor/getopt/getopt_compat.h)
    target_sources(${target} PRIVATE ${getopt_src})
    target_compile_definitions(${target} PRIVATE REPLACE_GETOPT)
    # Shim headers are scoped to the vendored TU only, so system headers
    # (glibc's sys/cdefs.h, err.h) are never shadowed for our own code.
    set_property(SOURCE ${getopt_src} APPEND PROPERTY
        INCLUDE_DIRECTORIES ${CMAKE_SOURCE_DIR}/vendor/getopt/compat)
    if(MSVC)
        target_include_directories(${target} PRIVATE
            ${CMAKE_SOURCE_DIR}/vendor/getopt/msvc-compat
        )
        set_property(SOURCE ${getopt_src} APPEND PROPERTY
            INCLUDE_DIRECTORIES ${CMAKE_SOURCE_DIR}/vendor/getopt/msvc-compat)
        set_property(SOURCE ${getopt_src} APPEND PROPERTY
            COMPILE_OPTIONS "/FI${getopt_compat}")
    else()
        set_property(SOURCE ${getopt_src} APPEND PROPERTY
            COMPILE_OPTIONS "-include" "${getopt_compat}")
    endif()
endfunction()

function(ctx_enable_cjson target)
    target_sources(${target} PRIVATE ${CMAKE_SOURCE_DIR}/vendor/cjson/cJSON.c)
    target_include_directories(${target} PRIVATE ${CMAKE_SOURCE_DIR}/vendor/cjson)
    # Third-party TU: keep our own warning flags off it so the build log stays
    # warning-free on every toolchain.
    set(cjson_src ${CMAKE_SOURCE_DIR}/vendor/cjson/cJSON.c)
    if(MSVC)
        set_property(SOURCE ${cjson_src} APPEND PROPERTY COMPILE_OPTIONS /w)
    else()
        set_property(SOURCE ${cjson_src} APPEND PROPERTY COMPILE_OPTIONS -w)
    endif()
endfunction()

add_executable(cctxpack
    src/main.c
    src/cli.c
    src/config.c
    src/cmd.c
    src/files.c
    src/glob.c
    src/read.c
    src/tokens.c
    src/pack.c
    src/score.c
    src/format.c
    src/util.c
)
target_include_directories(cctxpack PRIVATE src)
target_compile_definitions(cctxpack PRIVATE CTX_VERSION="${PROJECT_VERSION}")
ctx_enable_getopt(cctxpack)
ctx_enable_cjson(cctxpack)

if(NOT MSVC)
    target_link_libraries(cctxpack PRIVATE m)
endif()

enable_testing()

add_subdirectory(tests)
