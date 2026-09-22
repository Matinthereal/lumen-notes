#pragma once
#include <QString>

// Nightly backup (D-014): a zip of the data dir into ~/Backups/lumen, keeping the newest N.
// On Linux this runs as `lumen --backup` from a systemd --user timer; elsewhere (no systemd)
// main.cpp checks dueForNightlyBackup() on an in-app timer instead. Either way, "Back up now" in
// settings calls run() directly.
namespace backup {
struct Result { bool ok = false; QString path; QString error; int removed = 0; qint64 bytes = 0; };
Result run(const QString &dataDir, const QString &destDir, int keep = 7);
bool dueForNightlyBackup(const QString &destDir);   // no backup in destDir younger than 24h
#ifdef Q_OS_LINUX
bool installUserTimer(const QString &executable, QString *error = nullptr); // writes + enables the timer
bool timerInstalled();
bool timerNeedsUpdate(const QString &executable);   // missing, or pointing at another binary
#endif
}
