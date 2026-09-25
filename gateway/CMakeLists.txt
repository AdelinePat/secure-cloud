cmake_minimum_required(VERSION 3.20)
project(SecureChatGateway LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# ── Dependencies ───────────────────────────────────────────────────────────
find_package(OpenSSL REQUIRED)

# ── gateway library (all logic, no main) ────────────────────────────────────
# Everything under src/ except main.cpp. This is the actual fix versus your
# previous file: gateway_lib used to list src/main.cpp as its own source
# *and* the gateway executable compiled src/main.cpp again — two objects,
# same `main` symbol, duplicate-symbol error at link time. main.cpp is now
# exclusive to the executable target below.
file(GLOB_RECURSE GATEWAY_LIB_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)
list(FILTER GATEWAY_LIB_SOURCES EXCLUDE REGEX ".*/main\\.cpp$")

if(GATEWAY_LIB_SOURCES)
    add_library(gateway_lib STATIC
        ${GATEWAY_LIB_SOURCES}
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
else()
    message(STATUS "gateway: no sources under src/ yet (besides main.cpp) — gateway_lib not built")
endif()

# ── gateway executable ───────────────────────────────────────────────────────
add_executable(gateway
    src/main.cpp
)

target_include_directories(gateway PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

if(TARGET gateway_lib)
    target_link_libraries(gateway PRIVATE gateway_lib)
endif()

target_compile_options(gateway PRIVATE
    -Wall -Wextra -Wpedantic
)

# ── Tests ─────────────────────────────────────────────────────────────────────
enable_testing()
find_package(GTest REQUIRED)

file(GLOB_RECURSE GATEWAY_TEST_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp"
)

if(GATEWAY_TEST_SOURCES)
    add_executable(gateway_tests
        ${GATEWAY_TEST_SOURCES}
    )

    target_include_directories(gateway_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/tests
    )

    if(TARGET gateway_lib)
        target_link_libraries(gateway_tests PRIVATE gateway_lib)
    endif()

    target_link_libraries(gateway_tests PRIVATE
        GTest::gtest
        GTest::gmock
        GTest::gtest_main
    )

    target_compile_options(gateway_tests PRIVATE
        -Wall -Wextra -Wpedantic
    )

    add_test(NAME gateway_tests COMMAND gateway_tests)
else()
    message(STATUS "gateway: no sources under tests/ yet — gateway_tests not built")
endif()
