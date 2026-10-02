#pragma once
#include <QString>
class Database;
// Files by content hash: attachments/<h2>/<sha256>.<ext> (D-005).
namespace attachments {
// Returns the sha256, or empty. `name` is what the file is called in the library when the source's
// own name says nothing (a temporary file).
QString store(Database &db, const QString &sourcePath, const QString &mime, QString *error = nullptr, const QString &name = {});
QString pathFor(const QString &sha256, const QString &mime);
QString extFor(const QString &mime);
}
