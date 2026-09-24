add_executable(${PROJECT_NAME}_tests
    test.cpp
)

# Link against the project library and googletest.
# GTest::gtest_main provides a main() for the tests.
target_link_libraries(${PROJECT_NAME}_tests
    PRIVATE
    ${PROJECT_NAME}_lib
    GTest::gtest_main
)

# Aplicar flags de warning aos testes também
target_compile_options(${PROJECT_NAME}_tests PRIVATE ${PROJECT_WARNING_FLAGS})

# Descobre e registra os testes automaticamente
gtest_discover_tests(${PROJECT_NAME}_tests)

# Testes isolados do parser header-only: não dependem da aplicação ou WebView.
add_executable(option_parser_tests test_option_parser.cpp)
target_link_libraries(option_parser_tests
    PRIVATE
    app::cli_parser
    GTest::gtest_main
)
target_compile_options(option_parser_tests PRIVATE ${PROJECT_WARNING_FLAGS})
gtest_discover_tests(option_parser_tests)

add_executable(binary_rpc_tests test_binary_rpc.cpp)
target_include_directories(binary_rpc_tests PRIVATE "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(binary_rpc_tests PRIVATE ${PROJECT_NAME}_lib GTest::gtest_main)
target_compile_options(binary_rpc_tests PRIVATE ${PROJECT_WARNING_FLAGS})
gtest_discover_tests(binary_rpc_tests)

if(UNIX AND NOT APPLE)
    find_program(XVFB_RUN_EXECUTABLE xvfb-run)
    if(XVFB_RUN_EXECUTABLE)
        add_executable(binary_rpc_webview_tests test_binary_rpc_webview.cpp)
        target_link_libraries(binary_rpc_webview_tests PRIVATE ${PROJECT_NAME}_lib GTest::gtest_main)
        target_compile_definitions(binary_rpc_webview_tests PRIVATE APP_NO_EMBEDDED_UI=1)
        target_compile_options(binary_rpc_webview_tests PRIVATE ${PROJECT_WARNING_FLAGS})
        add_test(NAME binary_rpc_webview
            COMMAND ${XVFB_RUN_EXECUTABLE} -a $<TARGET_FILE:binary_rpc_webview_tests>)

        add_executable(dev_binary_rpc_webview_tests test_dev_binary_rpc_webview.cpp)
        target_link_libraries(dev_binary_rpc_webview_tests PRIVATE
            ${PROJECT_NAME}_lib GTest::gtest_main)
        target_compile_definitions(dev_binary_rpc_webview_tests PRIVATE
            APP_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
        target_compile_options(dev_binary_rpc_webview_tests PRIVATE ${PROJECT_WARNING_FLAGS})
        add_test(NAME dev_binary_rpc_webview
            COMMAND ${XVFB_RUN_EXECUTABLE} -a $<TARGET_FILE:dev_binary_rpc_webview_tests>)
        set_tests_properties(dev_binary_rpc_webview PROPERTIES TIMEOUT 60)
    endif()
endif()

# Exercise the same request, response, and event streams on every backend.
if(WIN32 OR APPLE OR XVFB_RUN_EXECUTABLE)
    add_executable(binary_rpc_platform_tests test_binary_rpc_platform.cpp)
    target_link_libraries(binary_rpc_platform_tests PRIVATE
        ${PROJECT_NAME}_lib GTest::gtest_main)
    target_compile_options(binary_rpc_platform_tests PRIVATE ${PROJECT_WARNING_FLAGS})
    if(XVFB_RUN_EXECUTABLE)
        add_test(NAME binary_rpc_platform
            COMMAND ${XVFB_RUN_EXECUTABLE} -a $<TARGET_FILE:binary_rpc_platform_tests>)
    else()
        add_test(NAME binary_rpc_platform COMMAND binary_rpc_platform_tests)
    endif()
    set_tests_properties(binary_rpc_platform PROPERTIES TIMEOUT 45)
endif()

# GTK/WebKit allocate process-lifetime caches during real WebView tests.
# Keep these suppressions scoped to the tests that instantiate a WebView;
# the pure C++ tests still run with unsuppressed leak detection.
if(XVFB_RUN_EXECUTABLE AND ENABLE_SANITIZERS AND
   EXISTS "${LSAN_SUPPRESSIONS_DST}")
    set_tests_properties(binary_rpc_webview binary_rpc_platform dev_binary_rpc_webview PROPERTIES
        ENVIRONMENT "LSAN_OPTIONS=suppressions=${LSAN_SUPPRESSIONS_DST}")
endif()
