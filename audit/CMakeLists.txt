cmake_minimum_required(VERSION 3.20)
project(SecureChatAudit LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# ── Dependencies ───────────────────────────────────────────────────────────
find_package(OpenSSL REQUIRED)
find_package(PostgreSQL REQUIRED)
find_package(libpqxx REQUIRED)

# set(SET_OF_FILE
#     src/file/file.cpp
# )

# ── audit library (all logic, no main) ──────────────────────────────────────
# add_library(audit_lib STATIC
#     # ${SET_OF_FILE}
# )

target_include_directories(audit_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(audit_lib PUBLIC
    OpenSSL::SSL
    OpenSSL::Crypto
    libpqxx::pqxx
)

target_compile_options(audit_lib PRIVATE
    -Wall -Wextra -Wpedantic
)

# ── audit executable ─────────────────────────────────────────────────────────
add_executable(audit
    src/main.cpp
)

target_link_libraries(audit PRIVATE audit_lib)

target_compile_options(audit PRIVATE
    -Wall -Wextra -Wpedantic
)

# ── Tests ─────────────────────────────────────────────────────────────────────
enable_testing()
find_package(GTest REQUIRED)

add_executable(audit_tests
)

target_include_directories(audit_tests PRIVATE
${CMAKE_CURRENT_SOURCE_DIR}/tests
)

target_link_libraries(audit_tests PRIVATE
audit_lib
GTest::gtest
GTest::gmock
GTest::gtest_main
)

target_compile_options(audit_tests PRIVATE
-Wall -Wextra -Wpedantic
)

add_test(NAME audit_tests COMMAND audit_tests)