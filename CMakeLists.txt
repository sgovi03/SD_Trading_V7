cmake_minimum_required(VERSION 3.14)
project(SDTradingSystem VERSION 8.0.0 LANGUAGES CXX)

# Build options
option(BUILD_TESTING    "Build tests (requires GoogleTest download)" OFF)
option(SD_ENABLE_CUDA   "Enable CUDA compute backend (RTX 4050)"    OFF)

# C++ Standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# CRITICAL: Set CMake policy for MSVC runtime library
if(POLICY CMP0091)
    cmake_policy(SET CMP0091 NEW)
endif()

# Force static runtime linking for MSVC (MUST BE EARLY)
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# Compiler flags (platform-specific)
if(MSVC)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /W4 /permissive- /FS")
    set(CMAKE_CXX_FLAGS_DEBUG   "${CMAKE_CXX_FLAGS_DEBUG}   /Zi /Od /DDEBUG_MODE /MTd /FS")
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /O2 /DNDEBUG /MT /FS")
else()
    set(CMAKE_CXX_FLAGS         "${CMAKE_CXX_FLAGS}         -Wall -Wextra -Wpedantic")
    set(CMAKE_CXX_FLAGS_DEBUG   "${CMAKE_CXX_FLAGS_DEBUG}   -g -O0 -DDEBUG_MODE")
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3 -DNDEBUG")
endif()

# Output directories
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

message(STATUS "==============================================")
message(STATUS "  SD Trading System V8.0 - Build Configuration")
message(STATUS "==============================================")
message(STATUS "Build type:   ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "Compiler:     ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
if(MSVC)
    message(STATUS "MSVC Runtime: ${CMAKE_MSVC_RUNTIME_LIBRARY}")
endif()
message(STATUS "==============================================")

# Include directories
include_directories(${PROJECT_SOURCE_DIR}/include)
include_directories(${PROJECT_SOURCE_DIR}/src)

# ============================================================
# DEPENDENCIES
# ============================================================

# nlohmann_json (V6.0 volume baseline + V8 symbol registry)
find_package(nlohmann_json CONFIG)
if(nlohmann_json_FOUND)
    message(STATUS "Found nlohmann_json")
else()
    message(WARNING "nlohmann_json not found - volume baseline and symbol registry will be disabled")
endif()

# jsoncpp - try multiple methods
find_package(jsoncpp CONFIG QUIET)
if(NOT jsoncpp_FOUND)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(jsoncpp jsoncpp)
    endif()
endif()

# ============================================================
# CURL + HTTP UTILS
# ============================================================
find_package(CURL REQUIRED)
message(STATUS "Found CURL: ${CURL_VERSION_STRING}")
add_subdirectory(src/utils/http)

# ============================================================
# V8: SQLite3
# Required for: bars, signals, trades, equity_curve tables.
# Option A (preferred): vcpkg — install with:
#   vcpkg install sqlite3:x64-windows
# Option B (fallback): bundled amalgamation
#   Place sqlite3.h + sqlite3.c in src/persistence/sqlite3/
# ============================================================
find_package(SQLite3 QUIET)
if(SQLite3_FOUND)
    message(STATUS "[V8] SQLite3 found via vcpkg: ${SQLite3_VERSION}")
else()
    # Fallback: bundled amalgamation
    set(SQLITE3_BUNDLED_DIR ${PROJECT_SOURCE_DIR}/src/persistence/sqlite3)
    if(EXISTS "${SQLITE3_BUNDLED_DIR}/sqlite3.c")
        message(STATUS "[V8] SQLite3 not found via vcpkg — using bundled amalgamation")
        add_library(sqlite3_bundled STATIC
            src/persistence/sqlite3/sqlite3.c
        )
        target_include_directories(sqlite3_bundled PUBLIC
            ${SQLITE3_BUNDLED_DIR}
        )
        target_compile_definitions(sqlite3_bundled PUBLIC
            SQLITE_THREADSAFE=2
            SQLITE_DEFAULT_WAL_SYNCHRONOUS=1
        )
        # Mimic the imported target so the rest of the file stays clean
        add_library(SQLite::SQLite3 ALIAS sqlite3_bundled)
        set(SQLite3_FOUND TRUE)
        message(STATUS "[V8] Bundled SQLite3 configured (sqlite3_bundled)")
    else()
        message(FATAL_ERROR
            "[V8] SQLite3 not found.\n"
            "  Option A (vcpkg): vcpkg install sqlite3:x64-windows\n"
            "  Option B (bundled): place sqlite3.h + sqlite3.c in "
            "src/persistence/sqlite3/")
    endif()
endif()

# Copy schema to build directory so verify tests can find it
configure_file(
    ${PROJECT_SOURCE_DIR}/src/persistence/db_schema.sql
    ${CMAKE_BINARY_DIR}/db_schema.sql
    COPYONLY
)

# ============================================================
# MAIN LIBRARY — sdcore
# All sources compiled into a single static library.
# V7 sources are unchanged. V8 sources appended below.
# ============================================================
set(LIBRARY_SOURCES

    # ----------------------------------------------------------
    # V7 CORE (unchanged)
    # ----------------------------------------------------------

    # Core
    src/core/config_loader.cpp
    src/core/system_config_loader.cpp      # <-- replaced with V8-patched version
    src/utils/system_initializer.cpp
    src/utils/logger.h
    src/core/zone_state_manager.h
    src/core/zone_state_manager.cpp

    # Utils — V6.0 volume baseline
    src/utils/volume_baseline.cpp
    src/utils/volume_baseline.h

    # Zones
    src/zones/zone_detector.cpp

    # Analysis
    src/analysis/market_analyzer.cpp

    # Scoring
    src/scoring/zone_scorer.cpp
    src/scoring/entry_decision_engine.cpp
    src/scoring/zone_quality_scorer.cpp
    src/scoring/entry_validation_scorer.cpp
    src/scoring/volume_scorer.cpp
    src/scoring/volume_scorer.h
    src/scoring/oi_scorer.cpp
    src/scoring/oi_scorer.h
    src/utils/institutional_index.h

    # Backtest
    src/backtest/trade_manager.cpp
    src/backtest/performance_tracker.cpp
    src/backtest/csv_reporter.cpp
    src/backtest/backtest_engine.cpp

    # Live (V7 — Fyers adapter kept for execution-only role)
    src/live/fyers_adapter.cpp
    src/live/live_engine.cpp

    # DryRun Engine
    src/csv_simulator_broker.cpp
    src/csv_simulator_broker.h
    src/dryrun/dryrun_engine.cpp
    src/dryrun/dryrun_engine.h

    # Unified Architecture
    src/ZonePersistenceAdapter.cpp
    src/ZonePersistenceAdapter.h
    src/EngineFactory.cpp
    src/EngineFactory.h
    src/ITradingEngine.h

    # ----------------------------------------------------------
    # V8 ADDITIONS
    # ----------------------------------------------------------

    # M1.1 — Three-tier config cascade (MASTER/classes/symbols)
    src/core/config_cascade.cpp

    # M0.2 — SQLite connection (WAL mode, prepared statements)
    src/persistence/sqlite_connection.cpp

    # M3.1 — Bar repository (bars + bar_rejections tables)
    src/persistence/sqlite_bar_repository.cpp

    # M3.3 — Trade repository (signals + trades + equity_curve)
    src/persistence/sqlite_trade_repository.cpp

    # M3.5 — DB writer (async EventBus → SQLite subscriber)
    src/persistence/db_writer.cpp

    # V8 — Zone repository (zones table upsert/update)
    src/persistence/sqlite_zone_repository.cpp

    # M2.3 — Bar validator (3-tier: structural / anomaly / sequence)
    src/data/bar_validator.cpp

    # M2.2 — Pipe listener (named pipe reader, one thread per symbol)
    src/data/pipe_listener.cpp

    # M2.6 — Shadow mode (AmiBroker vs Fyers parallel comparison)
    src/data/shadow_mode.cpp

    # M2.5 — AmiBroker adapter (data-only BrokerInterface)
    src/live/amibroker_adapter.cpp

    # SpringBoot bridge (M5)
    src/springboot/signal_consumer.cpp
    src/springboot/trade_close_listener.cpp
    src/springboot/springboot_bridge.cpp

    # Analytics (M6)
    src/analytics/metrics_aggregator.cpp

    # M6 — PortfolioGuard (shared pre-filter + EV lot sizing)
    src/scanner/portfolio_guard.cpp

    # M6 — SymbolContext (per-symbol lifecycle: config + PipeBroker + LiveEngine)
    src/scanner/symbol_context.cpp

    # M6 — BarRouter (routes BarValidatedEvent → correct PipeBroker)
    src/scanner/bar_router.cpp

    # M6 — PipeBroker (push-pipe adapter implementing BrokerInterface)
    src/live/pipe_broker.cpp

    # REMOVED: symbol_engine_context.cpp (replaced by LiveEngine via SymbolContext)
    # REMOVED: cpu_backend.cpp           (replaced by LiveEngine thread per symbol)
    # REMOVED: scanner_orchestrator.cpp  (replaced by BarRouter)
)

add_library(sdcore STATIC ${LIBRARY_SOURCES})
target_compile_definitions(sdcore PUBLIC SD_TRADING_V8=1)

target_include_directories(sdcore
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
        ${PROJECT_SOURCE_DIR}/src
)

# ── Core link dependencies ────────────────────────────────────
target_link_libraries(sdcore
    PUBLIC
        http_utils
        CURL::libcurl
        SQLite::SQLite3          # V8: persistence layer
)

# nlohmann_json (optional — volume baseline + symbol registry JSON)
if(nlohmann_json_FOUND)
    target_link_libraries(sdcore PUBLIC nlohmann_json::nlohmann_json)
endif()

# jsoncpp (optional)
if(jsoncpp_FOUND)
    if(TARGET jsoncpp_lib)
        target_link_libraries(sdcore PUBLIC jsoncpp_lib)
    elseif(TARGET jsoncpp::jsoncpp)
        target_link_libraries(sdcore PUBLIC jsoncpp::jsoncpp)
    else()
        target_link_libraries(sdcore PUBLIC ${jsoncpp_LIBRARIES})
        target_include_directories(sdcore PUBLIC ${jsoncpp_INCLUDE_DIRS})
    endif()
endif()

# Windows: named pipes need ws2_32
if(WIN32)
    target_link_libraries(sdcore PUBLIC ws2_32)
endif()

# ============================================================
# V7 EXECUTABLES (unchanged)
# ============================================================

add_executable(run_backtest src/backtest/run_backtest.cpp)
target_link_libraries(run_backtest PRIVATE sdcore)

add_executable(run_live src/live/run_live.cpp)
target_link_libraries(run_live PRIVATE sdcore)

add_executable(test_live_init src/test_live_init.cpp)
target_link_libraries(test_live_init PRIVATE sdcore)

add_executable(sd_trading_unified src/main_unified.cpp)
target_link_libraries(sd_trading_unified PRIVATE sdcore)

# ============================================================
# V8 LIVE RUNNER (M6 — LiveEngine per symbol architecture)
# ============================================================
add_executable(run_v8_live src/run_v8_live.cpp)
target_include_directories(run_v8_live PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(run_v8_live PRIVATE sdcore)
target_compile_features(run_v8_live PRIVATE cxx_std_17)
message(STATUS "[V8] run_v8_live: LiveEngine-per-symbol architecture")

# ============================================================
# TESTS (optional)
# ============================================================
if(BUILD_TESTING AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt")
    message(STATUS "Building tests (BUILD_TESTING=ON)")
    add_subdirectory(tests)
else()
    message(STATUS "Skipping tests (BUILD_TESTING=OFF)")
endif()

# ============================================================
# Summary
# ============================================================
message(STATUS "==============================================")
message(STATUS "  Configuration complete!")
message(STATUS "  Build targets:")
message(STATUS "    V7 (unchanged):")
message(STATUS "      - run_backtest")
message(STATUS "      - run_live")
message(STATUS "      - sd_trading_unified")
message(STATUS "    V8 (new):")
message(STATUS "      - verify_m0_m1  (schema + config cascade gate)")
message(STATUS "      - verify_m2     (bar validator + pipe + EventBus gate)")
message(STATUS "      - verify_m3     (SQLite repositories gate)")
message(STATUS "      - verify_m4     (scanner orchestrator gate)")
message(STATUS "      - run_v8_live   (M6: LiveEngine-per-symbol)")
message(STATUS "    SQLite3: ${SQLite3_FOUND}")
message(STATUS "    CUDA backend: ${SD_ENABLE_CUDA}")
if(BUILD_TESTING)
    message(STATUS "    - run_tests (test executable)")
else()
    message(STATUS "    - tests disabled (use -DBUILD_TESTING=ON to enable)")
endif()
message(STATUS "==============================================")
