cmake_minimum_required(VERSION 3.20)

project(SecureChatClient LANGUAGES CXX)

include(FetchContent)

# --------------------------------------------------
# C++ configuration
# --------------------------------------------------

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# --------------------------------------------------
# Platform
# --------------------------------------------------

if(WIN32)
    message(STATUS "SecureChat Client: Windows detected")
elseif(UNIX AND NOT APPLE)
    message(STATUS "SecureChat Client: Linux detected")
else()
    message(WARNING "SecureChat Client: untested platform ${CMAKE_SYSTEM_NAME}")
endif()

# --------------------------------------------------
# libsodium
# --------------------------------------------------

FetchContent_Declare(
    Sodium
    GIT_REPOSITORY https://github.com/robinlinden/libsodium-cmake.git
    GIT_TAG cfebfd3da486d5a86c644c8b47067e5411c7599c
)

set(SODIUM_DISABLE_TESTS ON CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(Sodium)

# --------------------------------------------------
# Qt
# --------------------------------------------------

find_package(Qt6 REQUIRED COMPONENTS Widgets)

qt_standard_project_setup()

# --------------------------------------------------
# Client
# --------------------------------------------------

add_library(securechat_crypto
    src/crypto/KeyManager.cpp
    src/crypto/MessageCrypto.cpp
    src/crypto/MessageSerializer.cpp
)

set_target_properties(securechat_crypto PROPERTIES
    AUTOMOC OFF
    AUTOUIC OFF
    AUTORCC OFF
)

qt_add_executable(client
    src/main.cpp
)

set_target_properties(client PROPERTIES
    AUTOMOC OFF
    AUTOUIC OFF
    AUTORCC OFF
)

target_include_directories(securechat_crypto PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(securechat_crypto
    PUBLIC
        sodium
)

target_link_libraries(client
    PRIVATE
        Qt6::Widgets
        securechat_crypto
)

# --------------------------------------------------
# Tests
# --------------------------------------------------

enable_testing()

add_executable(test_KeyManager
    tests/crypto/test_KeyManager.cpp
)

target_link_libraries(test_KeyManager
    PRIVATE
        securechat_crypto
)

set_target_properties(test_KeyManager PROPERTIES
    AUTOMOC OFF
    AUTOUIC OFF
    AUTORCC OFF
)

add_test(
    NAME KeyManagerTests
    COMMAND test_KeyManager
)

add_executable(test_MessageCrypto
    tests/crypto/test_MessageCrypto.cpp
)

set_target_properties(test_MessageCrypto PROPERTIES
    AUTOMOC OFF
    AUTOUIC OFF
    AUTORCC OFF
)

target_link_libraries(test_MessageCrypto
    PRIVATE
        securechat_crypto
)

add_test(
    NAME MessageCryptoTests
    COMMAND test_MessageCrypto
)

add_executable(test_MessageSerializer
    tests/crypto/test_MessageSerializer.cpp
)

set_target_properties(test_MessageSerializer PROPERTIES
    AUTOMOC OFF
    AUTOUIC OFF
    AUTORCC OFF
)

target_link_libraries(test_MessageSerializer
    PRIVATE
        securechat_crypto
)

add_test(
    NAME MessageSerializerTests
    COMMAND test_MessageSerializer
)