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

qt_add_executable(client
    src/main.cpp
    src/crypto/KeyManager.cpp
)

target_include_directories(client
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(client
    PRIVATE
        Qt6::Widgets
        sodium
)