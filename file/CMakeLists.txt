cmake_minimum_required(VERSION 3.20)
project(SecureChatFile LANGUAGES CXX)

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

# ── file library (all logic, no main) ──────────────────────────────────────
# add_library(file_lib STATIC
#     # ${SET_OF_FILE}
# )

target_include_directories(file_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(file_lib PUBLIC
    OpenSSL::SSL
    OpenSSL::Crypto
    libpqxx::pqxx
)

target_compile_options(file_lib PRIVATE
    -Wall -Wextra -Wpedantic
)

# ── file executable ─────────────────────────────────────────────────────────
add_executable(file
    src/main.cpp
)

target_link_libraries(file PRIVATE file_lib)

target_compile_options(file PRIVATE
    -Wall -Wextra -Wpedantic
)

# ── Tests ─────────────────────────────────────────────────────────────────────
enable_testing()
find_package(GTest REQUIRED)

add_executable(file_tests
)

target_include_directories(file_tests PRIVATE
${CMAKE_CURRENT_SOURCE_DIR}/tests
)

target_link_libraries(file_tests PRIVATE
file_lib
GTest::gtest
GTest::gmock
GTest::gtest_main
)

target_compile_options(file_tests PRIVATE
-Wall -Wextra -Wpedantic
)

add_test(NAME file_tests COMMAND file_tests)