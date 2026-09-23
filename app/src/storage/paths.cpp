#include "paths.h"
#include <QDir>
#include <QStandardPaths>

namespace paths {

QString dataDir()
{
    const QByteArray env = qgetenv("LUMEN_DATA_DIR");
    if (!env.isEmpty()) return QString::fromLocal8Bit(env);
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/lumen");
}
QString journalDir() { return dataDir() + QStringLiteral("/journal"); }
QString attachmentsDir() { return dataDir() + QStringLiteral("/attachments"); }
QString claudeLogDir() { return dataDir() + QStringLiteral("/claude-log"); }
QString modelsDir() { return dataDir() + QStringLiteral("/models"); }
// Thumbnails, rendered PDF pages and OCR results are keyed by one library's row ids, so a library
// in a folder of its own (LUMEN_DATA_DIR: the tests, a portable copy) keeps its cache in there too;
// sharing one would show another library's page 1 as this one's. The backup already skips it.
QString cacheDir()
{
    if (!qgetenv("LUMEN_DATA_DIR").isEmpty()) return dataDir() + QStringLiteral("/cache");
    return QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) + QStringLiteral("/lumen");
}
QString recordingsDir() { return dataDir() + QStringLiteral("/recordings"); }
QString databasePath() { return dataDir() + QStringLiteral("/lumen.db"); }
QString backupDir() { return QDir::homePath() + QStringLiteral("/Backups/lumen"); }

void ensureDirs()
{
    for (const QString &d : {dataDir(), journalDir(), attachmentsDir(), claudeLogDir(), modelsDir(), cacheDir(), recordingsDir()})
        QDir().mkpath(d);
}

} // namespace paths
