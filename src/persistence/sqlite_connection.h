// ============================================================
// SD TRADING V8 - SQLITE CONNECTION MANAGER
// src/persistence/sqlite_connection.h
//
// Milestone M0.2
//
// Design:
//   Single SQLite database file for the entire system.
//   WAL mode for concurrent reads (C++ engine + Spring Boot).
//   Prepared statement caching to avoid repeated compilation.
//   RAII wrapper: connection closes cleanly on destruction.
//
// Threading model:
//   One SqliteConnection instance per thread (not shared).
//   The DB writer thread owns the write connection.
//   Read-only connections may be created per-thread for queries.
//   WAL mode allows concurrent readers without blocking writer.
// ============================================================

#ifndef SDTRADING_SQLITE_CONNECTION_H
#define SDTRADING_SQLITE_CONNECTION_H

#include <string>
#include <memory>
#include <stdexcept>
#include <functional>
#include <unordered_map>
#include "../utils/logger.h"

// Forward declare SQLite types to avoid including sqlite3.h in headers
struct sqlite3;
struct sqlite3_stmt;

namespace SDTrading {
namespace Persistence {

// ============================================================
// EXCEPTIONS
// ============================================================

class SqliteException : public std::runtime_error {
public:
    explicit SqliteException(const std::string& msg) 
        : std::runtime_error(msg) {}
};

class SqliteConstraintException : public SqliteException {
public:
    explicit SqliteConstraintException(const std::string& msg)
        : SqliteException(msg) {}
};

// ============================================================
// PREPARED STATEMENT WRAPPER
// RAII wrapper - finalizes on destruction.
// ============================================================

class SqliteStatement {
public:
    explicit SqliteStatement(sqlite3_stmt* stmt);
    ~SqliteStatement();

    // Non-copyable, movable
    SqliteStatement(const SqliteStatement&) = delete;
    SqliteStatement& operator=(const SqliteStatement&) = delete;
    SqliteStatement(SqliteStatement&& other) noexcept;
    SqliteStatement& operator=(SqliteStatement&& other) noexcept;

    // Bind methods (1-indexed to match SQLite convention)
    SqliteStatement& bind_int(int index, int value);
    SqliteStatement& bind_int64(int index, int64_t value);
    SqliteStatement& bind_double(int index, double value);
    SqliteStatement& bind_text(int index, const std::string& value);
    SqliteStatement& bind_null(int index);
    SqliteStatement& bind_bool(int index, bool value);  // stores as 0/1

    // Execute (for INSERT/UPDATE/DELETE)
    // Returns number of rows affected.
    int execute();

    // Step (for SELECT - returns true if row available)
    bool step();

    // Column access (0-indexed)
    int         column_int(int index) const;
    int64_t     column_int64(int index) const;
    double      column_double(int index) const;
    std::string column_text(int index) const;
    bool        column_bool(int index) const;
    bool        column_is_null(int index) const;

    // Reset for re-use with new bindings
    void reset();

    // Get raw statement (for advanced use only)
    sqlite3_stmt* get() const { return stmt_; }

private:
    sqlite3_stmt* stmt_;
};

// ============================================================
// SQLITE CONNECTION
// One instance per thread. Owns all prepared statements
// created through it.
// ============================================================

class SqliteConnection {
public:
    // Open (or create) the database at db_path.
    // read_only: open in SQLITE_OPEN_READONLY mode (for query threads).
    explicit SqliteConnection(const std::string& db_path,
                              bool read_only = false);

    ~SqliteConnection();

    // Non-copyable, non-movable (owns raw sqlite3*)
    SqliteConnection(const SqliteConnection&) = delete;
    SqliteConnection& operator=(const SqliteConnection&) = delete;
    SqliteConnection(SqliteConnection&&) = delete;
    SqliteConnection& operator=(SqliteConnection&&) = delete;

    // --------------------------------------------------------
    // SCHEMA MANAGEMENT
    // --------------------------------------------------------

    // Apply schema from SQL file.
    // Safe to call on existing DB (all statements are CREATE IF NOT EXISTS).
    void apply_schema(const std::string& schema_sql_path);

    // Apply schema from in-memory SQL string.
    void apply_schema_string(const std::string& sql);

    // Get current schema version from schema_migrations table.
    int get_schema_version() const;

    // --------------------------------------------------------
    // STATEMENT PREPARATION
    // --------------------------------------------------------

    // Prepare a statement. Returns an owned SqliteStatement.
    // Caller must keep the statement alive for the duration of use.
    SqliteStatement prepare(const std::string& sql) const;

    // --------------------------------------------------------
    // TRANSACTION SUPPORT
    // --------------------------------------------------------

    void begin_transaction();
    void commit_transaction();
    void rollback_transaction();

    // Convenience: execute f() inside a transaction.
    // Commits on success, rolls back on exception.
    void with_transaction(std::function<void()> f);

    // --------------------------------------------------------
    // CONVENIENCE EXECUTE
    // For DDL and simple DML not needing parameter binding.
    // --------------------------------------------------------

    void execute_sql(const std::string& sql);

    // --------------------------------------------------------
    // UTILITY
    // --------------------------------------------------------

    // Returns the rowid of the last successful INSERT.
    int64_t last_insert_rowid() const;

    // Returns number of rows changed by last DML.
    int changes() const;

    // Check if connection is open.
    bool is_open() const { return db_ != nullptr; }

    // Get path to database file.
    const std::string& db_path() const { return db_path_; }

    // Raw handle (for advanced use only)
    sqlite3* handle() const { return db_; }

private:
    sqlite3*    db_;
    std::string db_path_;
    bool        in_transaction_;

    // Apply WAL mode and performance pragmas.
    void configure_pragmas();

    // Throw SqliteException with current SQLite error message.
    [[noreturn]] void throw_error(const std::string& context) const;
};

// ============================================================
// CONNECTION FACTORY
// Creates connections to the system database.
// DB path is set once at startup from system config.
// ============================================================

class SqliteConnectionFactory {
public:
    // Set the database path (called once at startup).
    static void initialize(const std::string& db_path,
                           const std::string& schema_sql_path);

    // Create a read-write connection (for the DB writer thread).
    static std::unique_ptr<SqliteConnection> create_write_connection();

    // Create a read-only connection (for query threads).
    static std::unique_ptr<SqliteConnection> create_read_connection();

    // Get the configured database path.
    static const std::string& get_db_path();

    static bool is_initialized() { return initialized_; }

private:
    static std::string db_path_;
    static std::string schema_sql_path_;
    static bool        initialized_;
};

} // namespace Persistence
} // namespace SDTrading

#endif // SDTRADING_SQLITE_CONNECTION_H
