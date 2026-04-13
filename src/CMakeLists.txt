# ==================================================================
# SD Trading V8 — src/CMakeLists.txt
# ==================================================================

add_subdirectory(core)
add_subdirectory(persistence)
add_subdirectory(events)
add_subdirectory(data)
add_subdirectory(scanner)
add_subdirectory(springboot)
add_subdirectory(analytics)

message(STATUS "[src] All subdirectories configured.")