#include "attachments.h"
#include "database.h"
#include "paths.h"
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace attachments {

QString extFor(const QString &mime)
{
    if (mime == "application/pdf") return "pdf";
    if (mime == "audio/ogg" || mime == "audio/opus") return "opus";
    if (mime == "image/png") return "png";
    if (mime == "image/jpeg") return "jpg";
    if (mime == "image/svg+xml") return "svg";
    return "bin";
}

QString pathFor(const QString &sha, const QString &mime)
{
    return QStringLiteral("%1/%2/%3.%4").arg(paths::attachmentsDir(), sha.left(2), sha, extFor(mime));
}

QString store(Database &db, const QString &sourcePath, const QString &mime, QString *error)
{
    QFile f(sourcePath);
    if (!f.open(QIODevice::ReadOnly)) { if (error) *error = "cannot read " + sourcePath; return {}; }
    QCryptographicHash h(QCryptographicHash::Sha256);
    if (!h.addData(&f)) { if (error) *error = "hash failed"; return {}; }
    const QString sha = QString::fromLatin1(h.result().toHex());
    const QString dest = pathFor(sha, mime);
    QDir().mkpath(QFileInfo(dest).absolutePath());
    if (!QFileInfo::exists(dest) || QFileInfo(dest).size() != QFileInfo(sourcePath).size()) {
        QFile::remove(dest + ".part");
        if (!QFile::copy(sourcePath, dest + ".part")) { if (error) *error = "cannot copy to " + dest; return {}; }
        QFile::remove(dest);
        if (!QFile::rename(dest + ".part", dest)) { if (error) *error = "cannot finalise " + dest; return {}; }   // a crash mid-copy leaves only .part
    }
    Database::Query q(db, "INSERT OR IGNORE INTO attachment(sha256, mime, bytes, name, created) VALUES (?,?,?,?,?)");
    q.bind(1, sha).bind(2, mime).bind(3, QFileInfo(sourcePath).size()).bind(4, QFileInfo(sourcePath).fileName()).bind(5, QDateTime::currentSecsSinceEpoch());
    q.run();
    return sha;
}

} // namespace attachments
