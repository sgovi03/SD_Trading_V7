# ==================================================================
# SD Trading V8 — src/CMakeLists.txt (M0–M3)
# ==================================================================

add_subdirectory(core)
add_subdirectory(persistence)
add_subdirectory(events)
add_subdirectory(data)

# M0.2 + M1.1 verification
add_executable(verify_m0_m1 verify_m0_m1.cpp)
target_include_directories(verify_m0_m1 PRIVATE
    ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(verify_m0_m1 PRIVATE sd_core sd_persistence)
target_compile_features(verify_m0_m1 PRIVATE cxx_std_17)

# M2 verification
add_executable(verify_m2 verify_m2.cpp)
target_include_directories(verify_m2 PRIVATE
    ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(verify_m2 PRIVATE sd_core sd_data sd_events)
target_compile_features(verify_m2 PRIVATE cxx_std_17)

# M3 verification
add_executable(verify_m3 verify_m3.cpp)
target_include_directories(verify_m3 PRIVATE
    ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(verify_m3 PRIVATE sd_core sd_persistence sd_events)
target_compile_features(verify_m3 PRIVATE cxx_std_17)

message(STATUS "[src] Executables: verify_m0_m1, verify_m2, verify_m3")
