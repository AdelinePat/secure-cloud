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

# ── file library (all logic, no main) ──────────────────────────────────────
# Everything under src/ except main.cpp — main.cpp stays exclusive to the
# `file` executable below, so it's never compiled into two objects at once.
file(GLOB_RECURSE FILE_LIB_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)
list(FILTER FILE_LIB_SOURCES EXCLUDE REGEX ".*/main\\.cpp$")

if(FILE_LIB_SOURCES)
    add_library(file_lib STATIC
        ${FILE_LIB_SOURCES}
    )

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
else()
    message(STATUS "file: no sources under src/ yet (besides main.cpp) — file_lib not built")
endif()

# ── file executable ─────────────────────────────────────────────────────────
add_executable(file
    src/main.cpp
)

target_include_directories(file PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

if(TARGET file_lib)
    target_link_libraries(file PRIVATE file_lib)
endif()

target_compile_options(file PRIVATE
    -Wall -Wextra -Wpedantic
)

# ── Tests ─────────────────────────────────────────────────────────────────────
enable_testing()
find_package(GTest REQUIRED)

file(GLOB_RECURSE FILE_TEST_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp"
)

if(FILE_TEST_SOURCES)
    add_executable(file_tests
        ${FILE_TEST_SOURCES}
    )

    target_include_directories(file_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/tests
    )

    if(TARGET file_lib)
        target_link_libraries(file_tests PRIVATE file_lib)
    endif()

    target_link_libraries(file_tests PRIVATE
        GTest::gtest
        GTest::gmock
        GTest::gtest_main
    )

    target_compile_options(file_tests PRIVATE
        -Wall -Wextra -Wpedantic
    )

    add_test(NAME file_tests COMMAND file_tests)
else()
    message(STATUS "file: no sources under tests/ yet — file_tests not built")
endif()
