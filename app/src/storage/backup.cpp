#include "backup.h"
#include "database.h"
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTextStream>
#if QT_CONFIG(process)
#include <QProcess>
#endif

namespace backup {

Result run(const QString &dataDir, const QString &destDir, int keep)
{
    Result r;
    if (!QDir().mkpath(destDir)) { r.error = QStringLiteral("cannot create %1").arg(destDir); return r; }
    // Fold the WAL so the copied .db is self-contained.
    { Database db; if (db.open(dataDir + "/lumen.db")) db.checkpoint(); }
    const QString stamp = QDateTime::currentDateTime().toString("yyyy-MM-dd_HHmm");
#if QT_CONFIG(process) && !defined(Q_OS_ANDROID)
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
#else
    r.path = QStringLiteral("%1/lumen-%2.tar").arg(destDir, stamp);
    if (!writeTar(dataDir, r.path, &r.error)) return r;
#endif
    r.bytes = QFileInfo(r.path).size();
    // Rotate: newest `keep` survive.
    QDir d(destDir);
    QStringList old = d.entryList({"lumen-*.zip", "lumen-*.tar.gz", "lumen-*.tar"}, QDir::Files, QDir::Name | QDir::Reversed);
    for (int i = keep; i < old.size(); ++i) if (d.remove(old[i])) ++r.removed;
    r.ok = true;
    return r;
}

// No systemd (or no timer set up yet): due if the newest backup in destDir is a day old or older.
// This is what drives the in-app timer on platforms without installUserTimer() below.
bool dueForNightlyBackup(const QString &destDir)
{
    QDir d(destDir);
    const QStringList files = d.entryList({"lumen-*.zip", "lumen-*.tar.gz", "lumen-*.tar"}, QDir::Files, QDir::Name | QDir::Reversed);
    if (files.isEmpty()) return true;
    const QDateTime newest = QFileInfo(d, files.first()).lastModified();
    return newest.msecsTo(QDateTime::currentDateTime()) >= qint64(24) * 3600 * 1000;
}

namespace {
// One ustar entry: a 512-byte header, then the data padded to a whole block. Exactly `size` bytes
// are written, so a file that grows meanwhile cannot shift the entries after it.
bool writeTarEntry(QIODevice &out, const QString &archiveName, const QString &source, QString *error)
{
    QFile in(source);
    if (!in.open(QIODevice::ReadOnly)) { *error = QStringLiteral("cannot read %1").arg(archiveName); return false; }
    // ustar keeps a name in two fields: up to 155 bytes of directories, then up to 100 of the rest.
    QByteArray name = archiveName.toUtf8(), prefix;
    if (name.size() > 100) {
        const qsizetype cut = name.lastIndexOf('/', 155);
        if (cut <= 0 || name.size() - cut - 1 > 100) { *error = QStringLiteral("name too long to back up: %1").arg(archiveName); return false; }
        prefix = name.left(cut);
        name = name.mid(cut + 1);
    }
    const qint64 size = in.size();
    QByteArray header(512, '\0');
    auto field = [&header](int at, const QByteArray &value) { header.replace(at, value.size(), value); };
    auto octal = [&field](int at, int width, qint64 value) { field(at, QByteArray::number(value, 8).rightJustified(width - 1, '0')); };
    field(0, name);
    octal(100, 8, 0644);
    octal(108, 8, 0);
    octal(116, 8, 0);
    octal(124, 12, size);
    octal(136, 12, QFileInfo(source).lastModified().toSecsSinceEpoch());
    field(148, QByteArray(8, ' '));                  // the checksum counts its own field as spaces
    header[156] = '0';
    field(257, QByteArrayLiteral("ustar"));          // "ustar\0", then version "00"
    field(263, QByteArrayLiteral("00"));
    field(345, prefix);
    unsigned sum = 0;
    for (char c : std::as_const(header)) sum += static_cast<unsigned char>(c);
    field(148, QByteArray::number(sum, 8).rightJustified(6, '0') + '\0');
    out.write(header);
    qint64 left = size;
    while (left > 0) {
        const QByteArray chunk = in.read(qMin<qint64>(left, 1 << 20));
        if (chunk.isEmpty()) break;
        out.write(chunk);
        left -= chunk.size();
    }
    // Shrank while being read: keep the archive well-formed; this one file is short.
    out.write(QByteArray(left + (512 - size % 512) % 512, '\0'));
    return true;
}
}

// POSIX ustar, uncompressed: the attachments are PDFs and photos that are compressed already. The
// database goes in as a VACUUM INTO snapshot, consistent even while the app is writing; the rest
// skips what the zip skips: the cache, downloaded models and half-written recordings.
bool writeTar(const QString &dataDir, const QString &tarPath, QString *error)
{
    QSaveFile out(tarPath);
    if (!out.open(QIODevice::WriteOnly)) { *error = QStringLiteral("cannot write %1").arg(tarPath); return false; }
    const QString snapshot = tarPath + QStringLiteral(".db");
    QFile::remove(snapshot);
    {
        Database db;
        QString quoted = snapshot;
        quoted.replace(QLatin1Char('\''), QStringLiteral("''"));
        if (!db.open(dataDir + "/lumen.db") || !db.exec(QStringLiteral("VACUUM INTO '%1'").arg(quoted))) {
            *error = QStringLiteral("cannot snapshot the database: %1").arg(db.lastError());
            return false;
        }
    }
    const bool dbOk = writeTarEntry(out, QStringLiteral("lumen.db"), snapshot, error);
    QFile::remove(snapshot);
    if (!dbOk) return false;
    const QDir root(dataDir);
    // A backup folder inside the data folder must not back up the backups, or the one being written.
    const QString destDir = QFileInfo(tarPath).absolutePath() + QLatin1Char('/');
    const bool destInside = destDir.startsWith(root.absolutePath() + QLatin1Char('/'));
    QDirIterator it(dataDir, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        const QString rel = root.relativeFilePath(path);
        if (rel.startsWith(QLatin1String("lumen.db")) || rel.startsWith(QLatin1String("cache/")) || rel.startsWith(QLatin1String("models/"))
            || (rel.startsWith(QLatin1String("recordings/")) && rel.endsWith(QLatin1String(".part")))
            || (destInside && QFileInfo(path).absoluteFilePath().startsWith(destDir)))
            continue;
        if (!writeTarEntry(out, rel, path, error)) return false;
    }
    out.write(QByteArray(1024, '\0'));               // two empty blocks end the archive
    if (!out.commit()) { *error = QStringLiteral("cannot finish %1").arg(tarPath); return false; }
    return true;
}

#if defined(Q_OS_LINUX) && !defined(Q_OS_ANDROID)

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

#endif // Q_OS_LINUX && !Q_OS_ANDROID

} // namespace backup
