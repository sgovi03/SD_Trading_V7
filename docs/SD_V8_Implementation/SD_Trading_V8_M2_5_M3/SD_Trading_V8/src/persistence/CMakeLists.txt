# ==================================================================
# SD Trading V8 — Persistence Library CMakeLists.txt
# Milestones M0.2 + M3
# ==================================================================

cmake_minimum_required(VERSION 3.16)

find_package(SQLite3 QUIET)

if(SQLite3_FOUND)
    message(STATUS "[Persistence] SQLite3 found: ${SQLite3_VERSION}")
else()
    message(STATUS "[Persistence] SQLite3 not found — using bundled amalgamation")
    set(SQLITE3_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/sqlite3)
    add_library(sqlite3_bundled STATIC sqlite3/sqlite3.c)
    target_include_directories(sqlite3_bundled PUBLIC sqlite3)
    target_compile_definitions(sqlite3_bundled PUBLIC SQLITE_THREADSAFE=2)
    set(SQLite3_LIBRARIES sqlite3_bundled)
    set(SQLite3_INCLUDE_DIRS ${SQLITE3_INCLUDE_DIR})
    set(SQLite3_FOUND TRUE)
endif()

add_library(sd_persistence STATIC
    sqlite_connection.cpp
    sqlite_bar_repository.cpp
    sqlite_trade_repository.cpp
    db_writer.cpp
)

target_include_directories(sd_persistence PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
    ${SQLite3_INCLUDE_DIRS}
)

target_link_libraries(sd_persistence PUBLIC
    ${SQLite3_LIBRARIES}
    sd_core
    sd_events
)

target_compile_features(sd_persistence PUBLIC cxx_std_17)

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/db_schema.sql
    ${CMAKE_BINARY_DIR}/db_schema.sql COPYONLY)

message(STATUS "[Persistence] sqlite_connection + bar/trade repos + db_writer")

find_library(DUCKDB_LIBRARY NAMES duckdb
    HINTS ${DUCKDB_ROOT}/lib /usr/local/lib /usr/lib)
find_path(DUCKDB_INCLUDE_DIR NAMES duckdb.h
    HINTS ${DUCKDB_ROOT}/include /usr/local/include /usr/include)
if(DUCKDB_LIBRARY AND DUCKDB_INCLUDE_DIR)
    message(STATUS "[Persistence] DuckDB found")
    target_compile_definitions(sd_persistence PUBLIC DUCKDB_AVAILABLE=1)
    target_include_directories(sd_persistence PUBLIC ${DUCKDB_INCLUDE_DIR})
    target_link_libraries(sd_persistence PUBLIC ${DUCKDB_LIBRARY})
else()
    message(STATUS "[Persistence] DuckDB not found — M6 analytics disabled")
endif()
