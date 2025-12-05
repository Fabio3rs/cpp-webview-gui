add_executable(${PROJECT_NAME}_tests test.cpp)

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
