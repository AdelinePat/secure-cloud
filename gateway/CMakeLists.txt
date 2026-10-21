cmake_minimum_required(VERSION 3.20)

project(SecureChatGateway LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Dependencies
find_package(OpenSSL REQUIRED)
find_package(Boost REQUIRED)
find_package(Threads REQUIRED)
find_package(GTest REQUIRED)

# Gateway library
add_library(gateway_lib STATIC
    src/HttpServer.cpp
    src/HttpResponse.cpp
    src/HttpRouter.cpp)

target_include_directories(gateway_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${Boost_INCLUDE_DIRS}
)

target_link_libraries(gateway_lib PUBLIC
    OpenSSL::SSL
    OpenSSL::Crypto
    Threads::Threads
)

target_compile_options(gateway_lib PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)

# Gateway executable
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
    -Wall
    -Wextra
    -Wpedantic
)

# Tests
enable_testing()

add_executable(gateway_tests
    tests/test_HttpServer.cpp
    tests/test_HttpResponse.cpp
    tests/test_HttpRouter.cpp
)

target_link_libraries(gateway_tests PRIVATE
    gateway_lib
    GTest::gtest
    GTest::gmock
    GTest::gtest_main
)

target_compile_options(gateway_tests PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)

add_test(
    NAME gateway_tests
    COMMAND gateway_tests
)
