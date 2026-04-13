# ==================================================================
# SD Trading V8 — Persistence Library CMakeLists.txt
# Milestone M0.2
#
# Replaces the previous stub:
#   "Persistence library: Not yet implemented (Milestone 4)"
#
# Dependencies:
#   SQLite3  — embedded database (WAL mode)
#   DuckDB   — analytical queries (optional, offline only)
# ==================================================================

cmake_minimum_required(VERSION 3.16)

# ----------------------------------------------------------------
# SQLite3 — find system installation or use bundled amalgamation
# ----------------------------------------------------------------

find_package(SQLite3 QUIET)

if(SQLite3_FOUND)
    message(STATUS "[Persistence] SQLite3 found: ${SQLite3_VERSION}")
else()
    # Fallback: use bundled sqlite3 amalgamation
    # Drop sqlite3.h and sqlite3.c into src/persistence/sqlite3/
    message(STATUS "[Persistence] SQLite3 not found via find_package — using bundled amalgamation")
    message(STATUS "[Persistence]   Expected: src/persistence/sqlite3/sqlite3.h")
    message(STATUS "[Persistence]   Expected: src/persistence/sqlite3/sqlite3.c")

    set(SQLITE3_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/sqlite3)
    set(SQLITE3_LIBRARY "sqlite3_bundled")

    add_library(sqlite3_bundled STATIC
        sqlite3/sqlite3.c
    )
    target_include_directories(sqlite3_bundled PUBLIC sqlite3)
    target_compile_definitions(sqlite3_bundled PUBLIC
        SQLITE_ENABLE_WAL_HOOK=1
        SQLITE_THREADSAFE=2          # Serialized per-connection (not global)
        SQLITE_DEFAULT_WAL_SYNCHRONOUS=1
    )
    set(SQLite3_LIBRARIES sqlite3_bundled)
    set(SQLite3_INCLUDE_DIRS ${SQLITE3_INCLUDE_DIR})
    set(SQLite3_FOUND TRUE)
endif()

# ----------------------------------------------------------------
# Persistence library
# ----------------------------------------------------------------

add_library(sd_persistence STATIC
    sqlite_connection.cpp
    # Added in M3:
    # sqlite_zone_repository.cpp
    # sqlite_trade_repository.cpp
    # sqlite_bar_repository.cpp
    # sqlite_session_repository.cpp
    # db_writer.cpp
)

target_include_directories(sd_persistence PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
    ${SQLite3_INCLUDE_DIRS}
)

target_link_libraries(sd_persistence PUBLIC
    ${SQLite3_LIBRARIES}
    sd_core      # for logger.h and common_types.h
)

target_compile_features(sd_persistence PUBLIC cxx_std_17)

# ----------------------------------------------------------------
# Schema file — copy to build directory for tests
# ----------------------------------------------------------------

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/db_schema.sql
    ${CMAKE_BINARY_DIR}/db_schema.sql
    COPYONLY
)

message(STATUS "[Persistence] Library: sd_persistence (SQLite WAL)")
message(STATUS "[Persistence] Schema:  db_schema.sql")

# ----------------------------------------------------------------
# DuckDB — optional analytical layer (offline only)
# ----------------------------------------------------------------

find_library(DUCKDB_LIBRARY NAMES duckdb
    HINTS ${DUCKDB_ROOT}/lib /usr/local/lib /usr/lib)
find_path(DUCKDB_INCLUDE_DIR NAMES duckdb.h
    HINTS ${DUCKDB_ROOT}/include /usr/local/include /usr/include)

if(DUCKDB_LIBRARY AND DUCKDB_INCLUDE_DIR)
    message(STATUS "[Persistence] DuckDB found: ${DUCKDB_LIBRARY}")
    target_compile_definitions(sd_persistence PUBLIC DUCKDB_AVAILABLE=1)
    target_include_directories(sd_persistence PUBLIC ${DUCKDB_INCLUDE_DIR})
    target_link_libraries(sd_persistence PUBLIC ${DUCKDB_LIBRARY})
    # Add when M6 analytics layer is implemented:
    # target_sources(sd_persistence PRIVATE duckdb_analytics.cpp)
else()
    message(STATUS "[Persistence] DuckDB not found — analytical layer disabled")
    message(STATUS "[Persistence]   Install DuckDB or set DUCKDB_ROOT to enable")
endif()
