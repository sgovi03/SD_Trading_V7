# ==================================================================
# SD Trading V8 — Events Module CMakeLists.txt
# src/events/CMakeLists.txt
# Milestone M2.4
#
# The EventBus and event_types are header-only.
# No compilation needed — just expose the include path.
# ==================================================================

# Header-only interface library
add_library(sd_events INTERFACE)

target_include_directories(sd_events INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)

target_compile_features(sd_events INTERFACE cxx_std_17)

message(STATUS "[Events] Header-only interface library: sd_events")
message(STATUS "[Events]   event_types.h  (M2.4 — all event structs)")
message(STATUS "[Events]   event_bus.h    (M2.4 — in-process pub/sub)")
