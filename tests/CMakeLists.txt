add_executable(tests
    test_main.c
    test_util.c
    test_getopt.c
    ${CMAKE_SOURCE_DIR}/src/util.c
    ${CMAKE_SOURCE_DIR}/vendor/getopt/getopt_long.c
)

target_include_directories(tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/vendor/greatest
)

target_compile_definitions(tests PRIVATE REPLACE_GETOPT)

set(getopt_src ${CMAKE_SOURCE_DIR}/vendor/getopt/getopt_long.c)
set(getopt_compat ${CMAKE_SOURCE_DIR}/vendor/getopt/getopt_compat.h)

# Shim headers are scoped to the vendored TU only, so system headers
# (glibc's sys/cdefs.h, err.h) are never shadowed for our own code.
set_property(SOURCE ${getopt_src} APPEND PROPERTY
    INCLUDE_DIRECTORIES ${CMAKE_SOURCE_DIR}/vendor/getopt/compat)

if(MSVC)
    target_include_directories(tests PRIVATE
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

add_test(NAME unit COMMAND tests)
