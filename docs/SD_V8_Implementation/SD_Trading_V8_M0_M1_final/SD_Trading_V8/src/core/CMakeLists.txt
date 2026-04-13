# ==================================================================
# SD Trading V8 — Core Library CMakeLists.txt
# src/core/CMakeLists.txt
#
# Replaces V7 version. Additions:
#   - config_cascade.cpp (M1.1)
#   - Links nlohmann/json (for SymbolRegistry JSON parsing)
# ==================================================================

add_library(sd_core STATIC
    config_loader.cpp
    system_config_loader.cpp         # Replace with system_config_loader_v8.cpp
    zone_state_manager.cpp
    config_cascade.cpp               # NEW — M1.1 three-tier config cascade
)

target_include_directories(sd_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include      # common_types.h, nlohmann/json.hpp
    ${CMAKE_SOURCE_DIR}/src          # for utils/logger.h
)

target_compile_features(sd_core PUBLIC cxx_std_17)

# nlohmann/json is header-only — just needs the include path.
# Already covered by CMAKE_SOURCE_DIR/include above (nlohmann/ subfolder).

target_compile_definitions(sd_core PUBLIC
    SD_TRADING_V8=1                  # Version guard — used by cascade code
)

message(STATUS "[Core] Library: sd_core")
message(STATUS "[Core]   config_loader.cpp")
message(STATUS "[Core]   system_config_loader.cpp (V8 patched)")
message(STATUS "[Core]   zone_state_manager.cpp")
message(STATUS "[Core]   config_cascade.cpp (M1.1 NEW)")
