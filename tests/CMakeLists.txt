add_executable(tests
    test_main.c
    test_util.c
    test_getopt.c
    test_files.c
    test_read.c
    test_tokens.c
    test_format.c
    test_config.c
    test_pack.c
    test_redact.c
    test_cli.c
    ${CMAKE_SOURCE_DIR}/src/cli.c
    ${CMAKE_SOURCE_DIR}/src/config.c
    ${CMAKE_SOURCE_DIR}/src/cmd.c
    ${CMAKE_SOURCE_DIR}/src/files.c
    ${CMAKE_SOURCE_DIR}/src/glob.c
    ${CMAKE_SOURCE_DIR}/src/read.c
    ${CMAKE_SOURCE_DIR}/src/tokens.c
    ${CMAKE_SOURCE_DIR}/src/pack.c
    ${CMAKE_SOURCE_DIR}/src/score.c
    ${CMAKE_SOURCE_DIR}/src/redact.c
    ${CMAKE_SOURCE_DIR}/src/format.c
    ${CMAKE_SOURCE_DIR}/src/util.c
)

target_include_directories(tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/vendor/greatest
)

target_compile_definitions(tests PRIVATE
    CTX_VERSION="${PROJECT_VERSION}"
    CTX_E2E_BIN="$<TARGET_FILE:cctxpack>"
    CTX_FIXTURE_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures/repo"
    CTX_BPE_FIXTURE_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures/bpe"
)

ctx_enable_getopt(tests)
ctx_enable_cjson(tests)
if(NOT MSVC)
    target_link_libraries(tests PRIVATE m)
endif()
add_dependencies(tests cctxpack)

add_test(NAME unit COMMAND tests)
