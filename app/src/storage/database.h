#pragma once
#include <QByteArray>
#include <QString>
#include <QVariant>
#include <functional>

struct sqlite3;
struct sqlite3_stmt;

// Thin, exception-free wrapper over the sqlite3 C API (D-004). One connection per Database object;
// the app uses exactly one. WAL, synchronous=NORMAL, foreign keys on.
class Database {
public:
    Database() = default;
    ~Database();
    Database(const Database &) = delete;
    Database &operator=(const Database &) = delete;

    bool open(const QString &path);
    void close();
    bool isOpen() const { return m_db != nullptr; }
    bool exec(const QString &sql);
    QString lastError() const { return m_error; }
    qint64 lastInsertId() const;
    int changes() const;
    bool begin();
    bool commit();
    bool rollback();
    bool checkpoint(); // WAL checkpoint (TRUNCATE)
    sqlite3 *handle() const { return m_db; }

    class Query {
    public:
        Query(Database &db, const QString &sql);
        ~Query();
        Query(const Query &) = delete;
        Query &operator=(const Query &) = delete;
        bool valid() const { return m_stmt != nullptr; }
        Query &bind(int index, qint64 v);
        Query &bind(int index, int v) { return bind(index, qint64(v)); }
        Query &bind(int index, double v);
        Query &bind(int index, const QString &v);
        Query &bind(int index, const QByteArray &blob);
        Query &bindNull(int index);
        bool step();          // true = a row is available
        bool run();           // step until done; true on success
        void reset();
        qint64 i64(int col) const;
        int i32(int col) const { return int(i64(col)); }
        double f64(int col) const;
        QString text(int col) const;
        QByteArray blob(int col) const;
        bool isNull(int col) const;
    private:
        Database &m_db;
        sqlite3_stmt *m_stmt = nullptr;
    };

private:
    sqlite3 *m_db = nullptr;
    QString m_error;
    friend class Query;
    void setError(const char *ctx);
};
