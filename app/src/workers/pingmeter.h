#pragma once
#include <QElapsedTimer>
#include <QObject>
#include <QString>

class WorkerSupervisor;
class QTimer;

// Pings a worker every 2 s and exposes the round trip to QML. Phase 0 proof that the
// app ⇄ Python path works and never blocks the UI.
class PingMeter : public QObject {
    Q_OBJECT
    Q_PROPERTY(double rttMs READ rttMs NOTIFY changed)
    Q_PROPERTY(int pongs READ pongs NOTIFY changed)
    Q_PROPERTY(QString workerPython READ workerPython NOTIFY changed)
    Q_PROPERTY(QString lastError READ lastError NOTIFY changed)
public:
    explicit PingMeter(WorkerSupervisor *worker, QObject *parent = nullptr);
    double rttMs() const { return m_rttMs; }
    int pongs() const { return m_pongs; }
    QString workerPython() const { return m_workerPython; }
    QString lastError() const { return m_lastError; }
signals:
    void changed();
private:
    void tick();
    WorkerSupervisor *m_worker;
    QTimer *m_timer;
    QElapsedTimer m_clock;
    int m_pendingId = -1;
    double m_rttMs = 0;
    int m_pongs = 0;
    QString m_workerPython;
    QString m_lastError;
};
