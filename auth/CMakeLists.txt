cmake_minimum_required(VERSION 3.20)
project(SecureChatAuth LANGUAGES CXX)

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

# ── auth library (all logic, no main) ──────────────────────────────────────
# add_library(auth_lib STATIC
#     # ${SET_OF_FILE}
# )

target_include_directories(auth_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(auth_lib PUBLIC
    OpenSSL::SSL
    OpenSSL::Crypto
    libpqxx::pqxx
)

target_compile_options(auth_lib PRIVATE
    -Wall -Wextra -Wpedantic
)

# ── auth executable ─────────────────────────────────────────────────────────
add_executable(auth
    src/main.cpp
)

target_link_libraries(auth PRIVATE auth_lib)

target_compile_options(auth PRIVATE
    -Wall -Wextra -Wpedantic
)

# ── Tests ─────────────────────────────────────────────────────────────────────
enable_testing()
find_package(GTest REQUIRED)

add_executable(auth_tests
)

target_include_directories(auth_tests PRIVATE
${CMAKE_CURRENT_SOURCE_DIR}/tests
)

target_link_libraries(auth_tests PRIVATE
auth_lib
GTest::gtest
GTest::gmock
GTest::gtest_main
)

target_compile_options(auth_tests PRIVATE
-Wall -Wextra -Wpedantic
)

add_test(NAME auth_tests COMMAND auth_tests)