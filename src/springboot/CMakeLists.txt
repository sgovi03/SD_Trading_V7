# ==================================================================
# SD Trading V8 — Spring Boot Bridge CMakeLists.txt
# src/springboot/CMakeLists.txt
# Milestone M5
# ==================================================================

add_library(sd_springboot STATIC
    signal_consumer.cpp        # M5 — EventBus → Spring Boot orders
    trade_close_listener.cpp   # M5 — SQLite poll → EventBus closes
    springboot_bridge.cpp      # M5 — top-level wiring
)

target_include_directories(sd_springboot PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(sd_springboot PUBLIC
    sd_core
    sd_events
    sd_persistence
    http_utils
    CURL::libcurl
)

target_compile_features(sd_springboot PUBLIC cxx_std_17)

message(STATUS "[SpringBoot] Library: sd_springboot")
message(STATUS "[SpringBoot]   signal_consumer.cpp")
message(STATUS "[SpringBoot]   trade_close_listener.cpp")
message(STATUS "[SpringBoot]   springboot_bridge.cpp")
