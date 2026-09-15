cmake_minimum_required(VERSION 3.20)
project(SecureChatMessaging LANGUAGES CXX)

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

# ── messaging library (all logic, no main) ──────────────────────────────────────
# add_library(messaging_lib STATIC
#     # ${SET_OF_FILE}
# )

target_include_directories(messaging_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(messaging_lib PUBLIC
    OpenSSL::SSL
    OpenSSL::Crypto
    libpqxx::pqxx
)

target_compile_options(messaging_lib PRIVATE
    -Wall -Wextra -Wpedantic
)

# ── messaging executable ─────────────────────────────────────────────────────────
add_executable(messaging
    src/main.cpp
)

target_link_libraries(messaging PRIVATE messaging_lib)

target_compile_options(messaging PRIVATE
    -Wall -Wextra -Wpedantic
)

# ── Tests ─────────────────────────────────────────────────────────────────────
enable_testing()
find_package(GTest REQUIRED)

add_executable(messaging_tests
)

target_include_directories(messaging_tests PRIVATE
${CMAKE_CURRENT_SOURCE_DIR}/tests
)

target_link_libraries(messaging_tests PRIVATE
messaging_lib
GTest::gtest
GTest::gmock
GTest::gtest_main
)

target_compile_options(messaging_tests PRIVATE
-Wall -Wextra -Wpedantic
)

add_test(NAME messaging_tests COMMAND messaging_tests)