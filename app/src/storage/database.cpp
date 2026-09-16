#include "database.h"
#include <QLoggingCategory>
#include <sqlite3.h>

Q_LOGGING_CATEGORY(lcDb, "lumen.db")

Database::~Database() { close(); }

void Database::setError(const char *ctx)
{
    m_error = QStringLiteral("%1: %2").arg(QString::fromUtf8(ctx), m_db ? QString::fromUtf8(sqlite3_errmsg(m_db)) : QStringLiteral("no db"));
    qCWarning(lcDb) << m_error;
}

bool Database::open(const QString &path)
{
    close();
    if (sqlite3_open_v2(path.toUtf8().constData(), &m_db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) != SQLITE_OK) {
        setError("open");
        sqlite3_close(m_db); m_db = nullptr;
        return false;
    }
    sqlite3_busy_timeout(m_db, 5000);
    return exec("PRAGMA journal_mode=WAL") && exec("PRAGMA synchronous=NORMAL") && exec("PRAGMA foreign_keys=ON")
        && exec("PRAGMA temp_store=MEMORY");
}

void Database::close()
{
    if (m_db) { sqlite3_close(m_db); m_db = nullptr; }
}

bool Database::exec(const QString &sql)
{
    if (!m_db) { m_error = "not open"; return false; }
    char *err = nullptr;
    const int rc = sqlite3_exec(m_db, sql.toUtf8().constData(), nullptr, nullptr, &err);
    if (rc != SQLITE_OK) {
        m_error = QStringLiteral("exec: %1 — %2").arg(QString::fromUtf8(err ? err : "?"), sql.left(80));
        qCWarning(lcDb) << m_error;
        sqlite3_free(err);
        return false;
    }
    return true;
}

qint64 Database::lastInsertId() const { return m_db ? sqlite3_last_insert_rowid(m_db) : 0; }
int Database::changes() const { return m_db ? sqlite3_changes(m_db) : 0; }
bool Database::begin() { return exec("BEGIN IMMEDIATE"); }
bool Database::commit() { return exec("COMMIT"); }
bool Database::rollback() { return exec("ROLLBACK"); }
bool Database::checkpoint() { return m_db && sqlite3_wal_checkpoint_v2(m_db, nullptr, SQLITE_CHECKPOINT_TRUNCATE, nullptr, nullptr) == SQLITE_OK; }

Database::Query::Query(Database &db, const QString &sql) : m_db(db)
{
    if (!db.m_db) return;
    if (sqlite3_prepare_v2(db.m_db, sql.toUtf8().constData(), -1, &m_stmt, nullptr) != SQLITE_OK) {
        db.setError("prepare");
        m_stmt = nullptr;
    }
}
Database::Query::~Query() { if (m_stmt) sqlite3_finalize(m_stmt); }
Database::Query &Database::Query::bind(int i, qint64 v) { if (m_stmt) sqlite3_bind_int64(m_stmt, i, v); return *this; }
Database::Query &Database::Query::bind(int i, double v) { if (m_stmt) sqlite3_bind_double(m_stmt, i, v); return *this; }
Database::Query &Database::Query::bind(int i, const QString &v)
{
    if (m_stmt) { const QByteArray u = v.toUtf8(); sqlite3_bind_text(m_stmt, i, u.constData(), u.size(), SQLITE_TRANSIENT); }
    return *this;
}
Database::Query &Database::Query::bind(int i, const QByteArray &b) { if (m_stmt) sqlite3_bind_blob(m_stmt, i, b.constData(), b.size(), SQLITE_TRANSIENT); return *this; }
Database::Query &Database::Query::bindNull(int i) { if (m_stmt) sqlite3_bind_null(m_stmt, i); return *this; }
bool Database::Query::step()
{
    if (!m_stmt) return false;
    const int rc = sqlite3_step(m_stmt);
    if (rc == SQLITE_ROW) return true;
    if (rc != SQLITE_DONE) m_db.setError("step");
    return false;
}
bool Database::Query::run()
{
    if (!m_stmt) return false;
    int rc;
    while ((rc = sqlite3_step(m_stmt)) == SQLITE_ROW) {}
    if (rc != SQLITE_DONE) { m_db.setError("run"); return false; }
    return true;
}
void Database::Query::reset() { if (m_stmt) { sqlite3_reset(m_stmt); sqlite3_clear_bindings(m_stmt); } }
qint64 Database::Query::i64(int c) const { return m_stmt ? sqlite3_column_int64(m_stmt, c) : 0; }
double Database::Query::f64(int c) const { return m_stmt ? sqlite3_column_double(m_stmt, c) : 0; }
QString Database::Query::text(int c) const { return m_stmt ? QString::fromUtf8(reinterpret_cast<const char *>(sqlite3_column_text(m_stmt, c))) : QString(); }
QByteArray Database::Query::blob(int c) const
{
    if (!m_stmt) return {};
    const void *d = sqlite3_column_blob(m_stmt, c);
    return QByteArray(static_cast<const char *>(d), sqlite3_column_bytes(m_stmt, c));
}
bool Database::Query::isNull(int c) const { return !m_stmt || sqlite3_column_type(m_stmt, c) == SQLITE_NULL; }
