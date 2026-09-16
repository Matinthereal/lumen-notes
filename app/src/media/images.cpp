#include "images.h"
#include "storage/attachments.h"
#include "storage/database.h"

#include <QClipboard>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QImageReader>
#include <QMimeDatabase>
#include <QTemporaryFile>

Images::Images(Database &db, QObject *parent) : QObject(parent), m_db(db) {}

QVariantList Images::list(qint64 pageId) const
{
    QVariantList out;
    Database::Query q(m_db, "SELECT i.id, i.attachment, a.mime, i.x, i.y, i.w, i.h FROM image i JOIN attachment a ON a.sha256=i.attachment"
                            " WHERE i.page_id=? ORDER BY i.id");
    q.bind(1, pageId);
    while (q.step())
        out.append(QVariantMap{{"id", q.i64(0)}, {"path", attachments::pathFor(q.text(1), q.text(2))},
                               {"x", q.f64(3)}, {"y", q.f64(4)}, {"w", q.f64(5)}, {"h", q.f64(6)}});
    return out;
}

int Images::count(qint64 pageId) const
{
    Database::Query q(m_db, "SELECT COUNT(*) FROM image WHERE page_id=?");
    q.bind(1, pageId);
    return q.step() ? q.i32(0) : 0;
}

QVariantMap Images::image(qint64 id) const
{
    Database::Query q(m_db, "SELECT i.id, i.attachment, a.mime, i.x, i.y, i.w, i.h, i.page_id FROM image i JOIN attachment a ON a.sha256=i.attachment WHERE i.id=?");
    q.bind(1, id);
    if (!q.step()) return {};
    return QVariantMap{{"id", q.i64(0)}, {"path", attachments::pathFor(q.text(1), q.text(2))},
                       {"x", q.f64(3)}, {"y", q.f64(4)}, {"w", q.f64(5)}, {"h", q.f64(6)}, {"pageId", q.i64(7)}};
}

qint64 Images::insertStored(qint64 pageId, const QString &sha, const QString &mime, QSize pixels, double x, double y, double maxWidth)
{
    if (sha.isEmpty() || pixels.isEmpty()) return 0;
    // Land at a sensible size: never wider than maxWidth, never upscaled past its own pixels.
    const double w = std::min<double>(maxWidth, pixels.width());
    const double h = w * double(pixels.height()) / double(pixels.width());
    Database::Query q(m_db, "INSERT INTO image(page_id, attachment, x, y, w, h) VALUES (?,?,?,?,?,?)");
    q.bind(1, pageId).bind(2, sha).bind(3, x).bind(4, y).bind(5, w).bind(6, h);
    if (!q.run()) { emit failed(QStringLiteral("could not place the picture")); return 0; }
    const qint64 id = m_db.lastInsertId();
    emit changed(pageId);
    return id;
}

qint64 Images::insertFile(qint64 pageId, const QUrl &file, double x, double y, double maxWidth)
{
    if (!pageId) return 0;
    const QString path = file.isLocalFile() ? file.toLocalFile() : file.toString();
    QImageReader reader(path);
    const QSize pixels = reader.size();
    if (!reader.canRead() || pixels.isEmpty()) { emit failed(QStringLiteral("that file is not a picture I can read")); return 0; }
    const QString mime = QMimeDatabase().mimeTypeForFile(path).name();
    QString err;
    const QString sha = attachments::store(m_db, path, mime, &err);
    if (sha.isEmpty()) { emit failed(err.isEmpty() ? QStringLiteral("could not copy the picture") : err); return 0; }
    return insertStored(pageId, sha, mime, pixels, x, y, maxWidth);
}

bool Images::clipboardHasImage() const
{
    const QClipboard *cb = QGuiApplication::clipboard();
    return cb && !cb->image().isNull();
}

qint64 Images::insertClipboard(qint64 pageId, double x, double y, double maxWidth)
{
    if (!pageId) return 0;
    const QImage img = QGuiApplication::clipboard()->image();
    if (img.isNull()) { emit failed(QStringLiteral("there is no picture on the clipboard")); return 0; }
    QTemporaryFile tmp(QDir::tempPath() + QStringLiteral("/lumen-paste-XXXXXX.png"));
    tmp.setAutoRemove(true);
    if (!tmp.open() || !img.save(tmp.fileName(), "PNG")) { emit failed(QStringLiteral("could not read the clipboard picture")); return 0; }
    tmp.close();
    QString err;
    const QString sha = attachments::store(m_db, tmp.fileName(), QStringLiteral("image/png"), &err);
    if (sha.isEmpty()) { emit failed(err.isEmpty() ? QStringLiteral("could not save the picture") : err); return 0; }
    return insertStored(pageId, sha, QStringLiteral("image/png"), img.size(), x, y, maxWidth);
}

void Images::setGeometry(qint64 id, double x, double y, double w, double h)
{
    const QVariantMap before = image(id);
    if (before.isEmpty()) return;
    Database::Query q(m_db, "UPDATE image SET x=?, y=?, w=?, h=? WHERE id=?");
    q.bind(1, x).bind(2, y).bind(3, std::max(8.0, w)).bind(4, std::max(8.0, h)).bind(5, id);
    if (q.run()) emit changed(before.value("pageId").toLongLong());
}

void Images::remove(qint64 id)
{
    const QVariantMap before = image(id);
    if (before.isEmpty()) return;
    Database::Query q(m_db, "DELETE FROM image WHERE id=?");
    q.bind(1, id);
    if (q.run()) emit changed(before.value("pageId").toLongLong());
}
