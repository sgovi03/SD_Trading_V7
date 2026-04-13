# ==================================================================
# SD Trading V8 — src/CMakeLists.txt (updated for M2)
# Adds sd_data and sd_events to the build.
# Add verify_m2 executable.
# ==================================================================

# Core subdirectories (already in M0/M1)
add_subdirectory(core)
add_subdirectory(persistence)

# M2 subdirectories
add_subdirectory(events)         # Header-only: event_types.h, event_bus.h
add_subdirectory(data)           # bar_validator.cpp, pipe_listener.cpp

# ── Verification executables ───────────────────────────────────

# M0.2 + M1.1 verification (from previous milestone)
add_executable(verify_m0_m1 verify_m0_m1.cpp)
target_include_directories(verify_m0_m1 PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(verify_m0_m1 PRIVATE sd_core sd_persistence)
target_compile_features(verify_m0_m1 PRIVATE cxx_std_17)

# M2 verification
add_executable(verify_m2 verify_m2.cpp)
target_include_directories(verify_m2 PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(verify_m2 PRIVATE
    sd_core
    sd_data
    sd_events
)
target_compile_features(verify_m2 PRIVATE cxx_std_17)

message(STATUS "[src] verify_m0_m1 — M0.2 + M1.1 gate test")
message(STATUS "[src] verify_m2    — M2 gate test")
