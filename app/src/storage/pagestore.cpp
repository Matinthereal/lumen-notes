#include "pagestore.h"
#include "database.h"
#include "ink/inkdocument.h"
#include "journal.h"
#include "strokecodec.h"
#include <QDateTime>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(lcStore, "lumen.store")

PageStore::PageStore(Database &db, const QString &journalDir, QObject *parent) : QObject(parent), m_db(db), m_dir(journalDir)
{
    m_idle.setSingleShot(true);
    m_idle.setInterval(2000);
    connect(&m_idle, &QTimer::timeout, this, [this] { flush(); });
}

PageStore::~PageStore() { unload(); }

void PageStore::attach(InkDocument *doc)
{
    m_doc = doc;
    connect(doc, &InkDocument::strokeAdded, this, [this](quint64 id, int index) {
        if (m_loading || !m_journal) return;
        if (const Stroke *s = m_doc->stroke(id)) journalFailed(!m_journal->appendAdd(index, *s));
        markDirty();
    });
    connect(doc, &InkDocument::strokeRemoved, this, [this](quint64 id, int) {
        if (m_loading || !m_journal) return;
        journalFailed(!m_journal->appendRemove(id));
        markDirty();
    });
    connect(doc, &InkDocument::strokesChanged, this, [this](const QVector<quint64> &ids) {
        if (m_loading || !m_journal) return;
        for (quint64 id : ids) if (const Stroke *s = m_doc->stroke(id)) journalFailed(!m_journal->appendUpdate(*s));
        markDirty();
    });
}

void PageStore::journalFailed(bool failed)
{
    if (!failed) return;
    if (!m_journalWarned) { m_journalWarned = true; emit storageError(QStringLiteral("Ink could not be written to disk (%1). Check free space — nothing since this message is safe until it clears.").arg(m_journal ? m_journal->path() : QString())); }
    m_idle.start(200);   // try to fold into the database sooner; that path reports its own errors
}

void PageStore::markDirty()
{
    if (!m_dirty) { m_dirty = true; emit dirtyChanged(); }
    m_idle.start();
}

int PageStore::journalFailures() const { return m_journal ? m_journal->appendFailures() : 0; }

bool PageStore::load(qint64 pageId)
{
    if (!m_doc) return false;
    if (m_pageId == pageId && m_pageId != 0) return true;
    unload();
    m_pageId = pageId;
    m_decodeFailed = false;
    m_journal = std::make_unique<PageJournal>(QStringLiteral("%1/%2.log").arg(m_dir).arg(pageId));
    m_loading = true;
    m_doc->clear();
    {
        Database::Query q(m_db, "SELECT data FROM stroke_blob WHERE page_id=?");
        q.bind(1, pageId);
        if (q.step()) {
            QVector<Stroke> strokes;
            const bool ok = strokecodec::decode(q.blob(0), strokes);
            for (Stroke &s : strokes) m_doc->addStroke(std::move(s));       // partial data is still data
            if (!ok) {
                m_decodeFailed = true;   // never write a truncated blob back over the original
                emit storageError(QStringLiteral("Page %1: some stroke data could not be read; the page is shown as far as it decodes and will not be overwritten").arg(pageId));
            }
        }
    }
    const int replayed = m_journal->replay(*m_doc);
    m_loading = false;
    if (replayed > 0 && !m_decodeFailed) {
        qCInfo(lcStore) << "page" << pageId << "recovered" << replayed << "journaled changes";
        m_dirty = true;
        flush();
    } else if (replayed < 0) {
        emit storageError(QStringLiteral("Page %1: journal unreadable, kept as is").arg(pageId));
    }
    m_journal->open();
    emit pageChanged();
    return true;
}

bool PageStore::flush()
{
    m_idle.stop();
    if (!m_dirty || !m_doc || m_pageId == 0) return true;
    if (m_decodeFailed) return false;                  // the journal keeps new ink; the blob stays untouched
    const QByteArray blob = strokecodec::encode(m_doc->strokes());
    if (!m_db.begin()) { emit storageError(m_db.lastError()); return false; }
    Database::Query q(m_db, "INSERT OR REPLACE INTO stroke_blob(page_id, schema, data, stroke_count) VALUES (?,?,?,?)");
    q.bind(1, m_pageId).bind(2, strokecodec::kVersion).bind(3, blob).bind(4, m_doc->count());
    Database::Query m(m_db, "UPDATE page SET modified=? WHERE id=?");
    m.bind(1, QDateTime::currentSecsSinceEpoch()).bind(2, m_pageId);
    if (!q.run() || !m.run() || !m_db.commit()) {
        m_db.rollback();
        emit storageError(m_db.lastError());
        return false;                       // the journal keeps everything; try again on the next idle
    }
    if (m_journal) m_journal->truncate();
    m_dirty = false;
    emit dirtyChanged();
    emit saved(m_pageId);
    return true;
}

void PageStore::unload()
{
    if (m_pageId == 0) return;
    flush();
    m_journal.reset();
    m_pageId = 0;
    emit pageChanged();
}
