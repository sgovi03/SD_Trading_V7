# ==================================================================
# SD Trading V8 — Scanner Library CMakeLists.txt
# src/scanner/CMakeLists.txt
# Milestone M4
# ==================================================================

add_library(sd_scanner STATIC
    symbol_engine_context.cpp    # M4.1 — per-symbol state
    cpu_backend.cpp              # M4.2 — thread pool
    scanner_orchestrator.cpp     # M4.3 — coordinator
)

target_include_directories(sd_scanner PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(sd_scanner PUBLIC
    sd_core
    sd_events
    sd_persistence
)

target_compile_features(sd_scanner PUBLIC cxx_std_17)

# CUDA backend (M7 — optional)
option(SD_ENABLE_CUDA "Enable CUDA compute backend (RTX 4050)" OFF)
if(SD_ENABLE_CUDA)
    find_package(CUDA REQUIRED)
    target_sources(sd_scanner PRIVATE cuda_backend.cu)
    target_compile_definitions(sd_scanner PUBLIC SD_CUDA_ENABLED=1)
    message(STATUS "[Scanner] CUDA backend enabled")
else()
    message(STATUS "[Scanner] CUDA backend disabled (use -DSD_ENABLE_CUDA=ON to enable)")
endif()

message(STATUS "[Scanner] Library: sd_scanner")
message(STATUS "[Scanner]   symbol_engine_context.cpp  (M4.1)")
message(STATUS "[Scanner]   cpu_backend.cpp            (M4.2 — ${CMAKE_THREAD_LIBS_INIT})")
message(STATUS "[Scanner]   scanner_orchestrator.cpp   (M4.3)")
