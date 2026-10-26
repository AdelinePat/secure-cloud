cmake_minimum_required(VERSION 3.20)
project(SecureChatAudit LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

include(FetchContent)

# ── Dependencies ───────────────────────────────────────────────────────────
find_package(OpenSSL REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(MONGOC REQUIRED IMPORTED_TARGET libmongoc-1.0 libbson-1.0)

# Header-only HTTP(S) server. OpenSSL is required: the service runs behind
# mutual TLS, and the peer certificate CN is the caller identity.
set(HTTPLIB_REQUIRE_OPENSSL ON CACHE BOOL "" FORCE)
set(HTTPLIB_INSTALL OFF CACHE BOOL "" FORCE)
set(HTTPLIB_TEST OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    httplib
    GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
    GIT_TAG v0.62.0
)
FetchContent_MakeAvailable(httplib)

# ── audit library (all logic, no main) ─────────────────────────────────────
file(GLOB_RECURSE AUDIT_LIB_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)
list(FILTER AUDIT_LIB_SOURCES EXCLUDE REGEX ".*/main\\.cpp$")

add_library(audit_lib STATIC
    ${AUDIT_LIB_SOURCES}
)

target_include_directories(audit_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(audit_lib PUBLIC
    OpenSSL::SSL
    OpenSSL::Crypto
    PkgConfig::MONGOC
    httplib::httplib
)

target_compile_options(audit_lib PRIVATE
    -Wall -Wextra -Wpedantic
)

# ── audit executable ───────────────────────────────────────────────────────
add_executable(audit
    src/main.cpp
)

target_link_libraries(audit PRIVATE audit_lib)

target_compile_options(audit PRIVATE
    -Wall -Wextra -Wpedantic
)

# ── Tests ──────────────────────────────────────────────────────────────────
enable_testing()
find_package(GTest REQUIRED)

file(GLOB_RECURSE AUDIT_TEST_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp"
)

if(AUDIT_TEST_SOURCES)
    add_executable(audit_tests
        ${AUDIT_TEST_SOURCES}
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
else()
    message(STATUS "audit: no sources under tests/ yet — audit_tests not built")
endif()
