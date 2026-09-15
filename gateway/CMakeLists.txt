cmake_minimum_required(VERSION 3.20)
project(SecureChatGateway LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# ── Dependencies ───────────────────────────────────────────────────────────
find_package(OpenSSL REQUIRED)

# set(SET_OF_FILE
#     src/file/file.cpp
# )

# ── gateway library (all logic, no main) ──────────────────────────────────────
add_library(gateway_lib STATIC
    # ${SET_OF_FILE}
    src/main.cpp
)

target_include_directories(gateway_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(gateway_lib PUBLIC
    OpenSSL::SSL
    OpenSSL::Crypto
)

target_compile_options(gateway_lib PRIVATE
    -Wall -Wextra -Wpedantic
)

# ── gateway executable ─────────────────────────────────────────────────────────
add_executable(gateway
    src/main.cpp
)

target_link_libraries(gateway PRIVATE gateway_lib)

target_compile_options(gateway PRIVATE
    -Wall -Wextra -Wpedantic
)

# ── Tests ─────────────────────────────────────────────────────────────────────
enable_testing()
find_package(GTest REQUIRED)

add_executable(gateway_tests
)

target_include_directories(gateway_tests PRIVATE
${CMAKE_CURRENT_SOURCE_DIR}/tests
)

target_link_libraries(gateway_tests PRIVATE
gateway_lib
GTest::gtest
GTest::gmock
GTest::gtest_main
)

target_compile_options(gateway_tests PRIVATE
-Wall -Wextra -Wpedantic
)

add_test(NAME gateway_tests COMMAND gateway_tests)