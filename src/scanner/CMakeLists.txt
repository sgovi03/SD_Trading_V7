# ==================================================================
# SD Trading V8 — Scanner Library CMakeLists.txt
# src/scanner/CMakeLists.txt
# M6 refactor: SymbolContext + PortfolioGuard + BarRouter
#
# REMOVED:
#   symbol_engine_context.cpp — replaced by LiveEngine via SymbolContext
#   cpu_backend.cpp           — replaced by LiveEngine thread per symbol
#   scanner_orchestrator.cpp  — replaced by BarRouter
# ==================================================================

add_library(sd_scanner STATIC
    portfolio_guard.cpp     # M6 — shared portfolio pre-filter + EV lot sizing
    symbol_context.cpp      # M6 — per-symbol lifecycle (PipeBroker + LiveEngine)
    bar_router.cpp          # M6 — routes BarValidatedEvent to correct PipeBroker
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

message(STATUS "[Scanner] Library: sd_scanner (M6 architecture)")
message(STATUS "[Scanner]   portfolio_guard.cpp  (shared PortfolioGuard)")
message(STATUS "[Scanner]   symbol_context.cpp   (SymbolContext: PipeBroker + LiveEngine)")
message(STATUS "[Scanner]   bar_router.cpp       (BarRouter: routing only)")