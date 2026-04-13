# ==================================================================
# SD Trading V8 — Analytics Library CMakeLists.txt
# src/analytics/CMakeLists.txt
# Milestone M6
# ==================================================================

add_library(sd_analytics STATIC
    metrics_aggregator.cpp     # M6.2 — SQLite-based metrics
    duckdb_analytics.cpp       # M6.1 — DuckDB heavy queries
)

target_include_directories(sd_analytics PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(sd_analytics PUBLIC
    sd_core
    sd_persistence
)

target_compile_features(sd_analytics PUBLIC cxx_std_17)

# DuckDB (optional — heavy analytics only)
find_library(DUCKDB_LIBRARY NAMES duckdb
    HINTS ${DUCKDB_ROOT}/lib /usr/local/lib /usr/lib
    "C:/duckdb/lib" "$ENV{USERPROFILE}/duckdb/lib")
find_path(DUCKDB_INCLUDE_DIR NAMES duckdb.h
    HINTS ${DUCKDB_ROOT}/include /usr/local/include /usr/include
    "C:/duckdb/include" "$ENV{USERPROFILE}/duckdb/include")

if(DUCKDB_LIBRARY AND DUCKDB_INCLUDE_DIR)
    message(STATUS "[Analytics] DuckDB found: ${DUCKDB_LIBRARY}")
    target_compile_definitions(sd_analytics PUBLIC DUCKDB_AVAILABLE=1)
    target_include_directories(sd_analytics PUBLIC ${DUCKDB_INCLUDE_DIR})
    target_link_libraries(sd_analytics PUBLIC ${DUCKDB_LIBRARY})
else()
    message(STATUS "[Analytics] DuckDB not found — heavy analytics disabled.")
    message(STATUS "[Analytics]   MetricsAggregator (SQLite) still fully functional.")
    message(STATUS "[Analytics]   To enable DuckDB: set DUCKDB_ROOT=<path>")
endif()

message(STATUS "[Analytics] Library: sd_analytics")
message(STATUS "[Analytics]   metrics_aggregator.cpp (M6.2 — always available)")
message(STATUS "[Analytics]   duckdb_analytics.cpp   (M6.1 — requires DuckDB)")
