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

find_package(Qt6 REQUIRED COMPONENTS
    Core
    Widgets
    Network
    Test
)

qt_standard_project_setup()

# --------------------------------------------------
# Client
# --------------------------------------------------

add_library(securechat_crypto
    src/crypto/KeyManager.cpp
    src/crypto/MessageCrypto.cpp
    src/crypto/MessageSerializer.cpp
    src/crypto/IdentityKeyStore.cpp
    src/crypto/PeerTrustStore.cpp
    src/crypto/FileCrypto.cpp
)

set_target_properties(securechat_crypto PROPERTIES
    AUTOMOC OFF
    AUTOUIC OFF
    AUTORCC OFF
)

qt_add_executable(client
    src/main.cpp
    src/ui/MainWindow.cpp
    include/ui/MainWindow.hpp
)

set_target_properties(client PROPERTIES
    AUTOMOC ON
    AUTOUIC OFF
    AUTORCC OFF
)

target_include_directories(client PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_include_directories(securechat_crypto PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(securechat_crypto
    PUBLIC
        sodium
        Qt6::Core
)

target_link_libraries(client
    PRIVATE
        Qt6::Widgets
        securechat_crypto
)

add_library(securechat_network
    src/network/MessageCodec.cpp
    src/network/SendMessageRequestCodec.cpp
    src/network/HttpClient.cpp
    src/network/SendMessageResultMapper.cpp
    src/network/MessageService.cpp
    src/network/ConversationService.cpp
    src/network/FileMessage.cpp
    src/network/FileService.cpp
    src/network/TlsTrust.cpp
    src/ui/MessageStatusMapper.cpp

    include/ui/MessageStatusMapper.hpp
    include/ui/MessageStatus.hpp
    include/network/MessageService.hpp
    include/network/ConversationService.hpp
    include/network/HttpClient.hpp
    include/network/FileService.hpp
    include/network/TlsTrust.hpp
)

target_include_directories(securechat_network PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(securechat_network
    PUBLIC
        Qt6::Core
        Qt6::Network
        securechat_crypto
)

set_target_properties(securechat_network PROPERTIES
    AUTOMOC ON
    AUTOUIC OFF
    AUTORCC OFF
)

target_link_libraries(client
    PRIVATE
        Qt6::Widgets
        securechat_network
)

# --------------------------------------------------
# Tests
# --------------------------------------------------

# --------------------------------------------------
# KeyManager tests
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

# --------------------------------------------------
# MessageCrypto tests
# --------------------------------------------------

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

# --------------------------------------------------
# MessageSerializer tests
# --------------------------------------------------

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

# --------------------------------------------------
# MessageCodec tests
# --------------------------------------------------

add_executable(test_MessageCodec
    tests/network/test_MessageCodec.cpp
)

set_target_properties(test_MessageCodec PROPERTIES
    AUTOMOC OFF
    AUTOUIC OFF
    AUTORCC OFF
)

target_link_libraries(test_MessageCodec
    PRIVATE
        securechat_network
)

add_test(
    NAME MessageCodecTests
    COMMAND test_MessageCodec
)

# --------------------------------------------------
# E2EE integration test
# --------------------------------------------------

add_executable(test_E2EEMessageFlow
    tests/integration/test_E2EEMessageFlow.cpp
)

set_target_properties(test_E2EEMessageFlow PROPERTIES
    AUTOMOC OFF
    AUTOUIC OFF
    AUTORCC OFF
)

target_link_libraries(test_E2EEMessageFlow
    PRIVATE
        securechat_network
)

add_test(
    NAME E2EEMessageFlowTests
    COMMAND test_E2EEMessageFlow
)

# --------------------------------------------------
# SendMessageRequestCodec tests
# --------------------------------------------------

add_executable(test_SendMessageRequestCodec
    tests/network/test_SendMessageRequestCodec.cpp
)

target_link_libraries(test_SendMessageRequestCodec
    PRIVATE
        securechat_network
)

add_test(
    NAME SendMessageRequestCodecTests
    COMMAND test_SendMessageRequestCodec
)

# --------------------------------------------------
# HttpClient tests
# --------------------------------------------------

add_executable(test_HttpClient
    tests/network/test_HttpClient.cpp
)

target_link_libraries(test_HttpClient
    PRIVATE
        securechat_network
        Qt6::Test
)

add_test(
    NAME HttpClientTests
    COMMAND test_HttpClient
)

# --------------------------------------------------
# SendMessageResultMapper tests
# --------------------------------------------------

add_executable(test_SendMessageResultMapper
    tests/network/test_SendMessageResultMapper.cpp
)

set_target_properties(test_SendMessageResultMapper PROPERTIES
    AUTOMOC ON
)

target_link_libraries(test_SendMessageResultMapper PRIVATE
    securechat_network
    Qt6::Test
    Qt6::Network
)

add_test(
    NAME SendMessageResultMapperTests
    COMMAND test_SendMessageResultMapper
)

# --------------------------------------------------
# MessageService tests
# --------------------------------------------------

add_executable(test_MessageService
    tests/network/test_MessageService.cpp
)

set_target_properties(test_MessageService PROPERTIES
    AUTOMOC ON
)

target_link_libraries(test_MessageService PRIVATE
    securechat_network
    Qt6::Test
    Qt6::Network
)

add_test(
    NAME MessageServiceTests
    COMMAND test_MessageService
)

# --------------------------------------------------
# MessageStatusMapper tests
# --------------------------------------------------

add_executable(test_MessageStatusMapper
    tests/ui/test_MessageStatusMapper.cpp
)

target_link_libraries(test_MessageStatusMapper PRIVATE
    securechat_network
    Qt6::Test
)

set_target_properties(test_MessageStatusMapper PROPERTIES
    AUTOMOC ON
)

add_test(
    NAME MessageStatusMapperTests
    COMMAND test_MessageStatusMapper
)

# --------------------------------------------------
# ConversationService tests
# --------------------------------------------------

add_executable(test_ConversationService
    tests/network/test_ConversationService.cpp
)

set_target_properties(test_ConversationService PROPERTIES
    AUTOMOC ON
)

target_link_libraries(test_ConversationService
    PRIVATE
        securechat_network
        Qt6::Test
        Qt6::Network
)

add_test(
    NAME ConversationServiceTests
    COMMAND test_ConversationService
)
# MainWindow authentication and conversation navigation regression tests.
add_executable(test_MainWindow
    tests/ui/test_MainWindow.cpp
    src/ui/MainWindow.cpp
    include/ui/MainWindow.hpp
)
target_link_libraries(test_MainWindow PRIVATE
    securechat_network Qt6::Widgets Qt6::Test
)
add_test(NAME MainWindowTests COMMAND test_MainWindow)
set_tests_properties(MainWindowTests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# --------------------------------------------------
# IdentityKeyStore tests
# --------------------------------------------------

add_executable(test_IdentityKeyStore
    tests/crypto/test_IdentityKeyStore.cpp
)

target_link_libraries(test_IdentityKeyStore
    PRIVATE
        securechat_crypto
)

add_test(
    NAME IdentityKeyStoreTests
    COMMAND test_IdentityKeyStore
)
add_executable(test_PeerTrustStore tests/crypto/test_PeerTrustStore.cpp)
target_link_libraries(test_PeerTrustStore PRIVATE securechat_crypto Qt6::Test)
add_test(NAME PeerTrustStoreTests COMMAND test_PeerTrustStore)

# --------------------------------------------------
# File encryption and transfer tests
# --------------------------------------------------

add_executable(test_FileCrypto tests/crypto/test_FileCrypto.cpp)
target_link_libraries(test_FileCrypto PRIVATE securechat_crypto Qt6::Test)
set_target_properties(test_FileCrypto PROPERTIES AUTOMOC ON)
add_test(NAME FileCryptoTests COMMAND test_FileCrypto)

add_executable(test_FileMessage tests/network/test_FileMessage.cpp)
target_link_libraries(test_FileMessage PRIVATE securechat_network Qt6::Test)
set_target_properties(test_FileMessage PROPERTIES AUTOMOC ON)
add_test(NAME FileMessageTests COMMAND test_FileMessage)

add_executable(test_FileService tests/network/test_FileService.cpp)
target_link_libraries(test_FileService PRIVATE securechat_network Qt6::Test Qt6::Network)
set_target_properties(test_FileService PROPERTIES AUTOMOC ON)
add_test(NAME FileServiceTests COMMAND test_FileService)

add_executable(test_TlsTrust tests/network/test_TlsTrust.cpp)
target_link_libraries(test_TlsTrust PRIVATE securechat_network Qt6::Test Qt6::Network)
set_target_properties(test_TlsTrust PROPERTIES AUTOMOC ON)
add_test(NAME TlsTrustTests COMMAND test_TlsTrust)
