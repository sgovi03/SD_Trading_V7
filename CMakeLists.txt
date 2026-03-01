cmake_minimum_required(VERSION 3.14)
project(SDTradingSystem VERSION 6.0.0 LANGUAGES CXX)

# Build options
option(BUILD_TESTING "Build tests (requires GoogleTest download)" OFF)

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
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /Zi /Od /DDEBUG_MODE /MTd /FS")
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /O2 /DNDEBUG /MT /FS")
else()
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wpedantic")
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g -O0 -DDEBUG_MODE")
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3 -DNDEBUG")
endif()

# Output directories
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

message(STATUS "==============================================")
message(STATUS "  SD Trading System V4.0 - Build Configuration")
message(STATUS "==============================================")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "Compiler: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
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

# nlohmann_json (V6.0 volume baseline)
find_package(nlohmann_json CONFIG)
if(nlohmann_json_FOUND)
    message(STATUS "Found nlohmann_json")
else()
    message(WARNING "nlohmann_json not found - volume baseline will be disabled")
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
# CRITICAL FIX: Use REQUIRED so CMake fails early with a clear
# message instead of building without http_utils then failing
# at link time with the cryptic LNK1104 error.
# ============================================================
find_package(CURL REQUIRED)
message(STATUS "Found CURL: ${CURL_VERSION_STRING}")

# http_utils is always built (CURL is now REQUIRED above)
add_subdirectory(src/utils/http)

# ============================================================
# MAIN LIBRARY
# ============================================================
set(LIBRARY_SOURCES
    # Core
    src/core/config_loader.cpp
    src/core/system_config_loader.cpp
    src/utils/system_initializer.cpp
    src/utils/logger.h
	src/core/zone_state_manager.h
	src/core/zone_state_manager.cpp

    # Utils - V6.0: Volume baseline infrastructure
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
    # V6.0: Volume & OI Scoring
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

    # Live
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
)

add_library(sdcore STATIC ${LIBRARY_SOURCES})

target_include_directories(sdcore
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
        ${PROJECT_SOURCE_DIR}/src
)

# ── Core link dependencies ────────────────────────────────────────────────────
# http_utils + CURL are always linked (CURL is REQUIRED above)
target_link_libraries(sdcore
    PUBLIC
    http_utils
    CURL::libcurl
)

# nlohmann_json (optional)
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

# ============================================================
# EXECUTABLES
# ============================================================

# Backtest Runner
add_executable(run_backtest
    src/backtest/run_backtest.cpp
)
target_link_libraries(run_backtest
    PRIVATE
    sdcore
)

# Live Trading Runner
add_executable(run_live
    src/live/run_live.cpp
)
target_link_libraries(run_live
    PRIVATE
    sdcore
)

# Test Live Init
add_executable(test_live_init
    src/test_live_init.cpp
)
target_link_libraries(test_live_init
    PRIVATE
    sdcore
)

# Unified Runner (backtest / dryrun / live)
add_executable(sd_trading_unified
    src/main_unified.cpp
)
target_link_libraries(sd_trading_unified
    PRIVATE
    sdcore
)

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
message(STATUS "    - sdcore (static library)")
message(STATUS "    - run_backtest (backtest runner)")
message(STATUS "    - run_live (live trading runner)")
message(STATUS "    - sd_trading_unified (unified runner)")
message(STATUS "      Modes: --mode=backtest|dryrun|live")
if(BUILD_TESTING)
    message(STATUS "    - run_tests (test executable)")
else()
    message(STATUS "    - tests disabled (use -DBUILD_TESTING=ON to enable)")
endif()
message(STATUS "==============================================")
