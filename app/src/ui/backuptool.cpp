#include "backuptool.h"
#include "storage/backup.h"
#include "storage/library.h"
#include "storage/paths.h"
BackupTool::BackupTool(Library &lib, QObject *parent) : QObject(parent), m_lib(lib) {}
QString BackupTool::runNow()
{
    const backup::Result r = backup::run(paths::dataDir(), m_lib.setting("backup.dir", paths::backupDir()), m_lib.setting("backup.keep", "7").toInt());
    return r.ok ? QStringLiteral("saved %1 (%2 MB)").arg(r.path).arg(r.bytes / 1048576.0, 0, 'f', 1) : QStringLiteral("failed: ") + r.error;
}
