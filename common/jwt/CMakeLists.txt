# Shared ES256 access-token verifier (tokens issued by the auth service).
# Kept outside the service trees so messaging/file verify tokens with one
# implementation.
#
# Used two ways:
#   - add_subdirectory() from a service: only the library is built.
#   - standalone (cmake -S common/jwt): library + jwt_tests.

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    cmake_minimum_required(VERSION 3.20)
    project(SecureChatJwt LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS OFF)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
    set(JWT_STANDALONE ON)
else()
    set(JWT_STANDALONE OFF)
endif()

option(JWT_BUILD_TESTS "Build the JWT verifier tests" ${JWT_STANDALONE})

find_package(OpenSSL REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_library(JWT_SODIUM_LIBRARY NAMES sodium REQUIRED)

add_library(securechat_jwt STATIC
    src/JwtVerifier.cpp
)

target_include_directories(securechat_jwt PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(securechat_jwt PUBLIC
    OpenSSL::Crypto
    nlohmann_json::nlohmann_json
    ${JWT_SODIUM_LIBRARY}
)

target_compile_features(securechat_jwt PUBLIC cxx_std_20)

target_compile_options(securechat_jwt PRIVATE
    -Wall -Wextra -Wpedantic
)

# ── Tests ──────────────────────────────────────────────────────────────────
if(JWT_BUILD_TESTS)
    enable_testing()
    find_package(GTest REQUIRED)

    add_executable(jwt_tests
        tests/JwtVerifierTest.cpp
    )

    target_link_libraries(jwt_tests PRIVATE
        securechat_jwt
        GTest::gtest
        GTest::gtest_main
    )

    target_compile_options(jwt_tests PRIVATE
        -Wall -Wextra -Wpedantic
    )

    add_test(NAME jwt_tests COMMAND jwt_tests)
endif()
