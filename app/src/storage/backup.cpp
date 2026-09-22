#include "backup.h"
#include "database.h"
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QTextStream>

namespace backup {

Result run(const QString &dataDir, const QString &destDir, int keep)
{
    Result r;
    if (!QDir().mkpath(destDir)) { r.error = QStringLiteral("cannot create %1").arg(destDir); return r; }
    // Fold the WAL so the copied .db is self-contained.
    { Database db; if (db.open(dataDir + "/lumen.db")) db.checkpoint(); }
    const QString stamp = QDateTime::currentDateTime().toString("yyyy-MM-dd_HHmm");
    r.path = QStringLiteral("%1/lumen-%2.zip").arg(destDir, stamp);
    const QString zip = QStandardPaths::findExecutable("zip");
    QProcess p;
    p.setWorkingDirectory(dataDir);
    if (!zip.isEmpty()) {
        p.start(zip, {"-q", "-r", "-y", r.path, ".", "-x", "cache/*", "models/*", "recordings/*.part"});
    } else {
        r.path.chop(4); r.path += ".tar.gz";
        p.start("tar", {"-czf", r.path, "--exclude=./cache", "--exclude=./models", "."});
    }
    if (!p.waitForFinished(600000) || p.exitCode() != 0) { r.error = QString::fromUtf8(p.readAllStandardError()).trimmed(); return r; }
    r.bytes = QFileInfo(r.path).size();
    // Rotate: newest `keep` survive.
    QDir d(destDir);
    QStringList old = d.entryList({"lumen-*.zip", "lumen-*.tar.gz"}, QDir::Files, QDir::Name | QDir::Reversed);
    for (int i = keep; i < old.size(); ++i) if (d.remove(old[i])) ++r.removed;
    r.ok = true;
    return r;
}

// No systemd (or no timer set up yet): due if the newest backup in destDir is a day old or older.
// This is what drives the in-app timer on platforms without installUserTimer() below.
bool dueForNightlyBackup(const QString &destDir)
{
    QDir d(destDir);
    const QStringList files = d.entryList({"lumen-*.zip", "lumen-*.tar.gz"}, QDir::Files, QDir::Name | QDir::Reversed);
    if (files.isEmpty()) return true;
    const QDateTime newest = QFileInfo(d, files.first()).lastModified();
    return newest.msecsTo(QDateTime::currentDateTime()) >= qint64(24) * 3600 * 1000;
}

#ifdef Q_OS_LINUX

static QString unitDir() { return QDir::homePath() + "/.config/systemd/user"; }

bool timerInstalled() { return QFileInfo::exists(unitDir() + "/lumen-backup.timer"); }

bool timerNeedsUpdate(const QString &executable)
{
    if (!timerInstalled()) return true;
    QFile svc(unitDir() + "/lumen-backup.service");
    if (!svc.open(QIODevice::ReadOnly | QIODevice::Text)) return true;
    return !QString::fromUtf8(svc.readAll()).contains("ExecStart=" + executable + " --backup");
}

bool installUserTimer(const QString &executable, QString *error)
{
    QDir().mkpath(unitDir());
    QFile svc(unitDir() + "/lumen-backup.service");
    if (!svc.open(QIODevice::WriteOnly | QIODevice::Text)) { if (error) *error = "cannot write service unit"; return false; }
    QTextStream(&svc) << "[Unit]\nDescription=Lumen nightly backup\n\n[Service]\nType=oneshot\nExecStart=" << executable << " --backup\nNice=10\n";
    svc.close();
    QFile tm(unitDir() + "/lumen-backup.timer");
    if (!tm.open(QIODevice::WriteOnly | QIODevice::Text)) { if (error) *error = "cannot write timer unit"; return false; }
    QTextStream(&tm) << "[Unit]\nDescription=Lumen nightly backup at 03:00\n\n[Timer]\nOnCalendar=*-*-* 03:00:00\nPersistent=true\nRandomizedDelaySec=10min\n\n[Install]\nWantedBy=timers.target\n";
    tm.close();
    QProcess p;
    p.start("systemctl", {"--user", "daemon-reload"}); p.waitForFinished(10000);
    p.start("systemctl", {"--user", "enable", "--now", "lumen-backup.timer"}); p.waitForFinished(10000);
    if (p.exitCode() != 0) { if (error) *error = QString::fromUtf8(p.readAllStandardError()).trimmed(); return false; }
    return true;
}

#endif // Q_OS_LINUX

} // namespace backup
