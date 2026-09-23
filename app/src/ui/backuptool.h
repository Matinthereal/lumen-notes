#pragma once
#include <QObject>
#include <QString>
class Library;
// "Back up now" from settings: runs the same backup as the timer, synchronously (it is quick).
// It also answers the everyday question — when was this last copied anywhere? — from the newest
// file in the backup folder, so the save indicator can say so without keeping its own bookkeeping.
class BackupTool : public QObject {
    Q_OBJECT
    Q_PROPERTY(qint64 lastBackup READ lastBackup NOTIFY changed)   // seconds since the epoch, 0 = never
    Q_PROPERTY(QString folder READ folder NOTIFY changed)
public:
    explicit BackupTool(Library &lib, QObject *parent = nullptr);
    Q_INVOKABLE QString runNow();
    qint64 lastBackup() const;
    QString folder() const;
signals:
    void changed();
private:
    Library &m_lib;
};
