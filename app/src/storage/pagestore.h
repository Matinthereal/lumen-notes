#pragma once
#include <QObject>
#include <QString>
#include <QTimer>
#include <memory>

class Database;
class InkDocument;
class PageJournal;

// Binds one InkDocument to one page row. Every mutation is journaled synchronously (fdatasync) so
// nothing acknowledged can be lost; the journal is folded into the stroke_blob row on idle (2 s),
// on page switch, and on quit (D-004).
class PageStore : public QObject {
    Q_OBJECT
    Q_PROPERTY(qint64 pageId READ pageId NOTIFY pageChanged)
    Q_PROPERTY(bool dirty READ dirty NOTIFY dirtyChanged)
public:
    PageStore(Database &db, const QString &journalDir, QObject *parent = nullptr);
    ~PageStore() override;
    void attach(InkDocument *doc);
    Q_INVOKABLE bool load(qint64 pageId);   // folds the current page first
    Q_INVOKABLE bool flush();               // fold now
    Q_INVOKABLE void unload();              // fold and detach from the page
    qint64 pageId() const { return m_pageId; }
    bool dirty() const { return m_dirty; }
    int idleMs() const { return m_idle.interval(); }
    void setIdleMs(int ms) { m_idle.setInterval(ms); }
    int journalFailures() const;

signals:
    void pageChanged();
    void dirtyChanged();
    void saved(qint64 pageId);
    void storageError(const QString &message);

private:
    void markDirty();
    void journalFailed(bool failed);
    Database &m_db;
    QString m_dir;
    InkDocument *m_doc = nullptr;
    std::unique_ptr<PageJournal> m_journal;
    qint64 m_pageId = 0;
    bool m_loading = false;
    bool m_dirty = false;
    bool m_decodeFailed = false;
    bool m_journalWarned = false;
    QTimer m_idle;
};
