#include "textblocks.h"
#include "storage/database.h"
#include "storage/library.h"
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>

TextBlocks::TextBlocks(Database &db, Library &lib, QObject *parent) : QObject(parent), m_db(db), m_lib(lib) {}

QVariantList TextBlocks::list(qint64 pageId) const
{
    QVariantList out;
    Database::Query q(m_db, "SELECT id, x, y, w, markdown, created_t, recording_id, edit_times FROM text_block WHERE page_id=? ORDER BY sort, id");
    q.bind(1, pageId);
    while (q.step())
        out.append(QVariantMap{{"id", q.i64(0)}, {"x", q.f64(1)}, {"y", q.f64(2)}, {"w", q.f64(3)}, {"markdown", q.text(4)},
                               {"createdT", q.i64(5)}, {"recordingId", q.i64(6)}, {"editTimes", q.text(7)}});
    return out;
}

QVariantMap TextBlocks::block(qint64 id) const
{
    Database::Query q(m_db, "SELECT id, page_id, x, y, w, markdown, created_t, recording_id FROM text_block WHERE id=?");
    q.bind(1, id);
    if (!q.step()) return {};
    return QVariantMap{{"id", q.i64(0)}, {"pageId", q.i64(1)}, {"x", q.f64(2)}, {"y", q.f64(3)}, {"w", q.f64(4)}, {"markdown", q.text(5)}, {"createdT", q.i64(6)}, {"recordingId", q.i64(7)}};
}

qint64 TextBlocks::create(qint64 pageId, double x, double y, double w, qint64 recordingId, qint64 tMs)
{
    Database::Query q(m_db, "INSERT INTO text_block(page_id, x, y, w, markdown, created_t, recording_id, sort, edit_times) VALUES (?,?,?,?,?,?,?,(SELECT COALESCE(MAX(sort),0)+1 FROM text_block WHERE page_id=?),'[]')");
    q.bind(1, pageId).bind(2, x).bind(3, y).bind(4, w).bind(5, QString()).bind(6, tMs).bind(7, recordingId).bind(8, pageId);
    if (!q.run()) return 0;
    emit changed(pageId);
    return m_db.lastInsertId();
}

void TextBlocks::setMarkdown(qint64 id, const QString &markdown, qint64 tMs)
{
    const QVariantMap b = block(id);
    if (b.isEmpty()) return;
    // Keep the timeline of edit batches (recording-relative ms, text length) for tap-to-seek (Phase 5).
    QJsonArray times;
    {
        Database::Query q(m_db, "SELECT edit_times FROM text_block WHERE id=?"); q.bind(1, id);
        if (q.step()) times = QJsonDocument::fromJson(q.text(0).toUtf8()).array();
    }
    if (tMs > 0) { times.append(QJsonArray{double(tMs), markdown.size()}); while (times.size() > 2000) times.removeFirst(); }
    Database::Query q(m_db, "UPDATE text_block SET markdown=?, edit_times=? WHERE id=?");
    q.bind(1, markdown).bind(2, QString::fromUtf8(QJsonDocument(times).toJson(QJsonDocument::Compact))).bind(3, id);
    q.run();
    Database::Query t(m_db, "UPDATE page SET modified=? WHERE id=?"); t.bind(1, QDateTime::currentSecsSinceEpoch()).bind(2, b.value("pageId").toLongLong()); t.run();
    m_lib.indexText("text", b.value("pageId").toLongLong(), id, markdown);
    m_lib.suggestTitle(b.value("pageId").toLongLong(), markdown);
}

void TextBlocks::setGeometry(qint64 id, double x, double y, double w)
{
    const QVariantMap b = block(id);
    if (b.isEmpty()) return;
    Database::Query q(m_db, "UPDATE text_block SET x=?, y=?, w=? WHERE id=?");
    q.bind(1, x).bind(2, y).bind(3, std::max(w, 80.0)).bind(4, id);
    if (q.run()) emit changed(b.value("pageId").toLongLong());
}

void TextBlocks::remove(qint64 id)
{
    const QVariantMap b = block(id);
    if (b.isEmpty()) return;
    m_lib.unindex("text", b.value("pageId").toLongLong(), id);
    Database::Query q(m_db, "DELETE FROM text_block WHERE id=?"); q.bind(1, id); q.run();
    emit changed(b.value("pageId").toLongLong());
}

QString TextBlocks::pageText(qint64 pageId) const
{
    QStringList parts;
    for (const QVariant &v : list(pageId)) { const QString m = v.toMap().value("markdown").toString(); if (!m.trimmed().isEmpty()) parts << m; }
    return parts.join("\n\n");
}
