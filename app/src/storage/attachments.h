#pragma once
#include <QString>
class Database;
// Files by content hash: attachments/<h2>/<sha256>.<ext> (D-005).
namespace attachments {
QString store(Database &db, const QString &sourcePath, const QString &mime, QString *error = nullptr); // returns sha256 or empty
QString pathFor(const QString &sha256, const QString &mime);
QString extFor(const QString &mime);
}
