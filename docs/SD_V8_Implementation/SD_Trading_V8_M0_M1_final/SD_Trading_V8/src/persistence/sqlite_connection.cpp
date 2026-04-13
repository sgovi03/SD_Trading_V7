// ============================================================
// SD TRADING V8 - SQLITE CONNECTION MANAGER IMPLEMENTATION
// src/persistence/sqlite_connection.cpp
// Milestone M0.2
// ============================================================

#include "sqlite_connection.h"
#include <sqlite3.h>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <cassert>

namespace SDTrading {
namespace Persistence {

// ============================================================
// SqliteStatement implementation
// ============================================================

SqliteStatement::SqliteStatement(sqlite3_stmt* stmt) : stmt_(stmt) {
    assert(stmt != nullptr);
}

SqliteStatement::~SqliteStatement() {
    if (stmt_) {
        sqlite3_finalize(stmt_);
        stmt_ = nullptr;
    }
}

SqliteStatement::SqliteStatement(SqliteStatement&& other) noexcept
    : stmt_(other.stmt_) {
    other.stmt_ = nullptr;
}

SqliteStatement& SqliteStatement::operator=(SqliteStatement&& other) noexcept {
    if (this != &other) {
        if (stmt_) sqlite3_finalize(stmt_);
        stmt_ = other.stmt_;
        other.stmt_ = nullptr;
    }
    return *this;
}

SqliteStatement& SqliteStatement::bind_int(int index, int value) {
    int rc = sqlite3_bind_int(stmt_, index, value);
    if (rc != SQLITE_OK) {
        throw SqliteException("bind_int failed at index " + std::to_string(index));
    }
    return *this;
}

SqliteStatement& SqliteStatement::bind_int64(int index, int64_t value) {
    int rc = sqlite3_bind_int64(stmt_, index, value);
    if (rc != SQLITE_OK) {
        throw SqliteException("bind_int64 failed at index " + std::to_string(index));
    }
    return *this;
}

SqliteStatement& SqliteStatement::bind_double(int index, double value) {
    int rc = sqlite3_bind_double(stmt_, index, value);
    if (rc != SQLITE_OK) {
        throw SqliteException("bind_double failed at index " + std::to_string(index));
    }
    return *this;
}

SqliteStatement& SqliteStatement::bind_text(int index, const std::string& value) {
    int rc = sqlite3_bind_text(stmt_, index, value.c_str(),
                               static_cast<int>(value.size()), SQLITE_TRANSIENT);
    if (rc != SQLITE_OK) {
        throw SqliteException("bind_text failed at index " + std::to_string(index));
    }
    return *this;
}

SqliteStatement& SqliteStatement::bind_null(int index) {
    int rc = sqlite3_bind_null(stmt_, index);
    if (rc != SQLITE_OK) {
        throw SqliteException("bind_null failed at index " + std::to_string(index));
    }
    return *this;
}

SqliteStatement& SqliteStatement::bind_bool(int index, bool value) {
    return bind_int(index, value ? 1 : 0);
}

int SqliteStatement::execute() {
    int rc = sqlite3_step(stmt_);
    if (rc != SQLITE_DONE && rc != SQLITE_ROW) {
        std::string errmsg = "execute() failed: ";
        errmsg += sqlite3_errmsg(sqlite3_db_handle(stmt_));
        sqlite3_reset(stmt_);
        if (rc == SQLITE_CONSTRAINT) {
            throw SqliteConstraintException(errmsg);
        }
        throw SqliteException(errmsg);
    }
    int changes = sqlite3_changes(sqlite3_db_handle(stmt_));
    sqlite3_reset(stmt_);
    return changes;
}

bool SqliteStatement::step() {
    int rc = sqlite3_step(stmt_);
    if (rc == SQLITE_ROW) return true;
    if (rc == SQLITE_DONE) return false;
    std::string errmsg = "step() failed: ";
    errmsg += sqlite3_errmsg(sqlite3_db_handle(stmt_));
    sqlite3_reset(stmt_);
    throw SqliteException(errmsg);
}

int SqliteStatement::column_int(int index) const {
    return sqlite3_column_int(stmt_, index);
}

int64_t SqliteStatement::column_int64(int index) const {
    return sqlite3_column_int64(stmt_, index);
}

double SqliteStatement::column_double(int index) const {
    return sqlite3_column_double(stmt_, index);
}

std::string SqliteStatement::column_text(int index) const {
    const unsigned char* text = sqlite3_column_text(stmt_, index);
    if (!text) return "";
    return std::string(reinterpret_cast<const char*>(text));
}

bool SqliteStatement::column_bool(int index) const {
    return sqlite3_column_int(stmt_, index) != 0;
}

bool SqliteStatement::column_is_null(int index) const {
    return sqlite3_column_type(stmt_, index) == SQLITE_NULL;
}

void SqliteStatement::reset() {
    sqlite3_reset(stmt_);
    sqlite3_clear_bindings(stmt_);
}

// ============================================================
// SqliteConnection implementation
// ============================================================

SqliteConnection::SqliteConnection(const std::string& db_path, bool read_only)
    : db_(nullptr), db_path_(db_path), in_transaction_(false) {

    int flags = read_only
        ? (SQLITE_OPEN_READONLY | SQLITE_OPEN_NOMUTEX)
        : (SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_NOMUTEX);

    int rc = sqlite3_open_v2(db_path.c_str(), &db_, flags, nullptr);
    if (rc != SQLITE_OK) {
        std::string err = "Failed to open SQLite database '";
        err += db_path + "': ";
        if (db_) {
            err += sqlite3_errmsg(db_);
            sqlite3_close(db_);
            db_ = nullptr;
        } else {
            err += "out of memory";
        }
        throw SqliteException(err);
    }

    if (!read_only) {
        configure_pragmas();
    }

    LOG_INFO("[SQLite] Opened database: " << db_path
             << (read_only ? " (read-only)" : " (read-write, WAL)"));
}

SqliteConnection::~SqliteConnection() {
    if (in_transaction_) {
        try { rollback_transaction(); } catch (...) {}
    }
    if (db_) {
        sqlite3_close_v2(db_);
        db_ = nullptr;
    }
}

void SqliteConnection::configure_pragmas() {
    // WAL mode: allows concurrent reads while writing
    execute_sql("PRAGMA journal_mode = WAL");
    // NORMAL sync: safe with WAL, much faster than FULL
    execute_sql("PRAGMA synchronous = NORMAL");
    // Foreign key enforcement
    execute_sql("PRAGMA foreign_keys = ON");
    // 32 MB page cache
    execute_sql("PRAGMA cache_size = -32000");
    // Temp tables in memory
    execute_sql("PRAGMA temp_store = MEMORY");
    // WAL checkpoint interval: every 1000 pages
    execute_sql("PRAGMA wal_autocheckpoint = 1000");
}

void SqliteConnection::apply_schema(const std::string& schema_sql_path) {
    std::ifstream file(schema_sql_path);
    if (!file.is_open()) {
        throw SqliteException("Cannot open schema file: " + schema_sql_path);
    }
    std::ostringstream ss;
    ss << file.rdbuf();
    apply_schema_string(ss.str());
}

void SqliteConnection::apply_schema_string(const std::string& sql) {
    char* errmsg = nullptr;
    int rc = sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &errmsg);
    if (rc != SQLITE_OK) {
        std::string err = "Schema application failed: ";
        if (errmsg) {
            err += errmsg;
            sqlite3_free(errmsg);
        }
        throw SqliteException(err);
    }
    LOG_INFO("[SQLite] Schema applied successfully");
}

int SqliteConnection::get_schema_version() const {
    const char* sql =
        "SELECT MAX(version) FROM schema_migrations WHERE 1=1";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK || !stmt) return 0;

    int version = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        version = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return version;
}

SqliteStatement SqliteConnection::prepare(const std::string& sql) const {
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK || !stmt) {
        std::string err = "prepare() failed for SQL [";
        err += sql.substr(0, 80) + "...]: ";
        err += sqlite3_errmsg(db_);
        throw SqliteException(err);
    }
    return SqliteStatement(stmt);
}

void SqliteConnection::begin_transaction() {
    if (in_transaction_) {
        throw SqliteException("begin_transaction() called while already in transaction");
    }
    execute_sql("BEGIN IMMEDIATE");
    in_transaction_ = true;
}

void SqliteConnection::commit_transaction() {
    if (!in_transaction_) {
        throw SqliteException("commit_transaction() called without active transaction");
    }
    execute_sql("COMMIT");
    in_transaction_ = false;
}

void SqliteConnection::rollback_transaction() {
    if (!in_transaction_) return;  // Safe no-op
    execute_sql("ROLLBACK");
    in_transaction_ = false;
}

void SqliteConnection::with_transaction(std::function<void()> f) {
    begin_transaction();
    try {
        f();
        commit_transaction();
    } catch (...) {
        try { rollback_transaction(); } catch (...) {}
        throw;
    }
}

void SqliteConnection::execute_sql(const std::string& sql) {
    char* errmsg = nullptr;
    int rc = sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &errmsg);
    if (rc != SQLITE_OK) {
        std::string err = "execute_sql() failed: ";
        if (errmsg) {
            err += errmsg;
            sqlite3_free(errmsg);
        }
        err += " | SQL: " + sql.substr(0, 100);
        if (rc == SQLITE_CONSTRAINT) {
            throw SqliteConstraintException(err);
        }
        throw SqliteException(err);
    }
}

int64_t SqliteConnection::last_insert_rowid() const {
    return sqlite3_last_insert_rowid(db_);
}

int SqliteConnection::changes() const {
    return sqlite3_changes(db_);
}

[[noreturn]] void SqliteConnection::throw_error(const std::string& context) const {
    std::string err = context + ": ";
    err += db_ ? sqlite3_errmsg(db_) : "no database handle";
    throw SqliteException(err);
}

// ============================================================
// SqliteConnectionFactory implementation
// ============================================================

std::string SqliteConnectionFactory::db_path_;
std::string SqliteConnectionFactory::schema_sql_path_;
bool        SqliteConnectionFactory::initialized_ = false;

void SqliteConnectionFactory::initialize(const std::string& db_path,
                                          const std::string& schema_sql_path) {
    db_path_ = db_path;
    schema_sql_path_ = schema_sql_path;
    initialized_ = true;

    // Create and apply schema on first run (idempotent)
    auto conn = create_write_connection();
    conn->apply_schema(schema_sql_path_);

    LOG_INFO("[SQLite] Factory initialized. DB: " << db_path_
             << " | Schema version: " << conn->get_schema_version());
}

std::unique_ptr<SqliteConnection>
SqliteConnectionFactory::create_write_connection() {
    if (!initialized_) {
        throw SqliteException("SqliteConnectionFactory not initialized. "
                              "Call initialize() at startup.");
    }
    return std::make_unique<SqliteConnection>(db_path_, false);
}

std::unique_ptr<SqliteConnection>
SqliteConnectionFactory::create_read_connection() {
    if (!initialized_) {
        throw SqliteException("SqliteConnectionFactory not initialized.");
    }
    return std::make_unique<SqliteConnection>(db_path_, true);
}

const std::string& SqliteConnectionFactory::get_db_path() {
    return db_path_;
}

} // namespace Persistence
} // namespace SDTrading
