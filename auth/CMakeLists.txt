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

# ── auth library (all logic, no main) ──────────────────────────────────────
# Everything under src/ except main.cpp — main.cpp stays exclusive to the
# `auth` executable below, so it's never compiled into two objects at once
# (that duplicate-symbol pattern is what broke gateway's CMakeLists).
file(GLOB_RECURSE AUTH_LIB_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)
list(FILTER AUTH_LIB_SOURCES EXCLUDE REGEX ".*/main\\.cpp$")

if(AUTH_LIB_SOURCES)
    add_library(auth_lib STATIC
        ${AUTH_LIB_SOURCES}
    )

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
else()
    message(STATUS "auth: no sources under src/ yet (besides main.cpp) — auth_lib not built")
endif()

# ── auth executable ─────────────────────────────────────────────────────────
add_executable(auth
    src/main.cpp
)

target_include_directories(auth PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

if(TARGET auth_lib)
    target_link_libraries(auth PRIVATE auth_lib)
endif()

target_compile_options(auth PRIVATE
    -Wall -Wextra -Wpedantic
)

# ── Tests ─────────────────────────────────────────────────────────────────────
enable_testing()
find_package(GTest REQUIRED)

file(GLOB_RECURSE AUTH_TEST_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp"
)

if(AUTH_TEST_SOURCES)
    add_executable(auth_tests
        ${AUTH_TEST_SOURCES}
    )

    target_include_directories(auth_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/tests
    )

    if(TARGET auth_lib)
        target_link_libraries(auth_tests PRIVATE auth_lib)
    endif()

    target_link_libraries(auth_tests PRIVATE
        GTest::gtest
        GTest::gmock
        GTest::gtest_main
    )

    target_compile_options(auth_tests PRIVATE
        -Wall -Wextra -Wpedantic
    )

    add_test(NAME auth_tests COMMAND auth_tests)
else()
    message(STATUS "auth: no sources under tests/ yet — auth_tests not built")
endif()
