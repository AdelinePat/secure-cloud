# Shared best-effort audit emitter, built into each emitting microservice.
# Kept outside the service trees so auth/file/messaging share one client.

find_package(OpenSSL REQUIRED)
find_package(Boost REQUIRED COMPONENTS system)
find_package(nlohmann_json CONFIG REQUIRED)

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
)

target_compile_features(securechat_audit_client PUBLIC cxx_std_20)

target_compile_options(securechat_audit_client PRIVATE
    -Wall -Wextra -Wpedantic
)
