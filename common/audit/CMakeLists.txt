# Shared audit emitter (durable spool + background delivery), built into each
# emitting microservice. Kept outside the service trees so auth/file/messaging
# share one client.
#
# Used two ways:
#   - add_subdirectory() from a service: only the library is built.
#   - standalone (cmake -S common/audit): library + audit_client_tests.

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    cmake_minimum_required(VERSION 3.20)
    project(SecureChatAuditClient LANGUAGES CXX)
    if(POLICY CMP0167)
        cmake_policy(SET CMP0167 NEW)  # use Boost's own BoostConfig.cmake
    endif()
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS OFF)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
    set(AUDIT_CLIENT_STANDALONE ON)
else()
    set(AUDIT_CLIENT_STANDALONE OFF)
endif()

option(AUDIT_CLIENT_BUILD_TESTS "Build the audit client tests"
    ${AUDIT_CLIENT_STANDALONE})

find_package(OpenSSL REQUIRED)
find_package(Boost CONFIG REQUIRED COMPONENTS system)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(Threads REQUIRED)

add_library(securechat_audit_client STATIC
    src/AuditClient.cpp
)

target_include_directories(securechat_audit_client PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(securechat_audit_client PUBLIC
    OpenSSL::SSL
    OpenSSL::Crypto
    Boost::system
    nlohmann_json::nlohmann_json
    Threads::Threads
)

target_compile_features(securechat_audit_client PUBLIC cxx_std_20)

target_compile_options(securechat_audit_client PRIVATE
    -Wall -Wextra -Wpedantic
)

# ── Tests ──────────────────────────────────────────────────────────────────
if(AUDIT_CLIENT_BUILD_TESTS)
    enable_testing()
    find_package(GTest REQUIRED)

    add_executable(audit_client_tests
        tests/AuditClientTest.cpp
    )

    target_link_libraries(audit_client_tests PRIVATE
        securechat_audit_client
        GTest::gtest
        GTest::gtest_main
    )

    target_compile_options(audit_client_tests PRIVATE
        -Wall -Wextra -Wpedantic
    )

    add_test(NAME audit_client_tests COMMAND audit_client_tests)
endif()
