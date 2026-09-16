#pragma once
#include <QString>

// Data lives in $LUMEN_DATA_DIR, else $XDG_DATA_HOME/lumen (SPEC §4). Subdirectories are
// created on first use.
namespace paths {
QString dataDir();
QString journalDir();
QString attachmentsDir();
QString claudeLogDir();
QString modelsDir();
QString cacheDir();
QString recordingsDir();
QString databasePath();
QString backupDir();          // ~/Backups/lumen (Q21) unless the setting overrides it
void ensureDirs();
}
