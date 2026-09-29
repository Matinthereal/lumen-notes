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
// Helper models are downloads of hundreds of MB that belong to no library, so test runs that each
// get a fresh LUMEN_DATA_DIR share one folder of them through LUMEN_MODELS_DIR.
QString modelsDir()
{
    const QByteArray env = qgetenv("LUMEN_MODELS_DIR");
    if (!env.isEmpty()) return QString::fromLocal8Bit(env);
    return dataDir() + QStringLiteral("/models");
}
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
QString backupDir()
{
#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
    // The app's own Documents: on an iPad that is Files › On My iPad › Lumen › Backups.
    return QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + QStringLiteral("/Backups");
#else
    return QDir::homePath() + QStringLiteral("/Backups/lumen");
#endif
}

void ensureDirs()
{
    for (const QString &d : {dataDir(), journalDir(), attachmentsDir(), claudeLogDir(), modelsDir(), cacheDir(), recordingsDir()})
        QDir().mkpath(d);
}

QString openable(const QUrl &picked) { return picked.isLocalFile() ? picked.toLocalFile() : picked.toString(); }

} // namespace paths
