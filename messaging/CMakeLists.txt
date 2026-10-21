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

# ── messaging library (all logic, no main) ──────────────────────────────────
# Everything under src/ except main.cpp — main.cpp stays exclusive to the
# `messaging` executable below, so it's never compiled into two objects at once.
file(GLOB_RECURSE MESSAGING_LIB_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)
list(FILTER MESSAGING_LIB_SOURCES EXCLUDE REGEX ".*/main\\.cpp$")

if(MESSAGING_LIB_SOURCES)
    add_library(messaging_lib STATIC
        ${MESSAGING_LIB_SOURCES}
    )

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
else()
    message(STATUS "messaging: no sources under src/ yet (besides main.cpp) — messaging_lib not built")
endif()

# ── messaging executable ─────────────────────────────────────────────────────
add_executable(messaging
    src/main.cpp
)

target_include_directories(messaging PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

if(TARGET messaging_lib)
    target_link_libraries(messaging PRIVATE messaging_lib)
endif()

target_compile_options(messaging PRIVATE
    -Wall -Wextra -Wpedantic
)

# ── Tests ─────────────────────────────────────────────────────────────────────
enable_testing()
find_package(GTest REQUIRED)

file(GLOB_RECURSE MESSAGING_TEST_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp"
)

if(MESSAGING_TEST_SOURCES)
    add_executable(messaging_tests
        ${MESSAGING_TEST_SOURCES}
    )

    target_include_directories(messaging_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/tests
    )

    if(TARGET messaging_lib)
        target_link_libraries(messaging_tests PRIVATE messaging_lib)
    endif()

    target_link_libraries(messaging_tests PRIVATE
        GTest::gtest
        GTest::gmock
        GTest::gtest_main
    )

    target_compile_options(messaging_tests PRIVATE
        -Wall -Wextra -Wpedantic
    )

    add_test(NAME messaging_tests COMMAND messaging_tests)
else()
    message(STATUS "messaging: no sources under tests/ yet — messaging_tests not built")
endif()
