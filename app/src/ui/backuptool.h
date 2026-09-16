#pragma once
#include <QObject>
#include <QString>
class Library;
// "Back up now" from settings: runs the same backup as the timer, synchronously (it is quick).
class BackupTool : public QObject {
    Q_OBJECT
public:
    explicit BackupTool(Library &lib, QObject *parent = nullptr);
    Q_INVOKABLE QString runNow();
private:
    Library &m_lib;
};
