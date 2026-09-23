#pragma once
#include <QElapsedTimer>
#include <QJsonObject>
#include <QLocalServer>
#include <QMap>
#include <QSet>
#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QString>
#include "ipc/jsonlines.h"

class QLocalSocket;
class QTimer;

// Owns one Python worker process (D-006): a Unix socket the worker connects to, newline-JSON
// request/response with integer ids, restart with backoff when the process dies. The UI thread
// never waits on it; answers arrive as signals.
//
// It also keeps what a person needs to see about the process: whether its Python packages are
// there at all (a worker that dies on an import is "not installed", not crashed, and is not
// restarted in a loop), what it is doing right now from the worker's progress notifications, and
// a way to cancel a request the worker honours between steps.
class WorkerSupervisor : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString name READ name CONSTANT)
    Q_PROPERTY(QString state READ stateName NOTIFY stateChanged)
    Q_PROPERTY(QString status READ status NOTIFY activityChanged)     // what the Background services panel says
    Q_PROPERTY(QString python READ pythonPath CONSTANT)
    Q_PROPERTY(int restarts READ restarts NOTIFY stateChanged)
    Q_PROPERTY(QStringList missing READ missing NOTIFY stateChanged)                 // packages it cannot work without
    Q_PROPERTY(QStringList missingOptional READ missingOptional NOTIFY stateChanged) // packages some of its features need
    Q_PROPERTY(QString lastError READ lastError NOTIFY stateChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY activityChanged)
    Q_PROPERTY(QString activity READ activity NOTIFY activityChanged)
    Q_PROPERTY(qreal progress READ progress NOTIFY activityChanged)  // 0..1, or -1 when the worker cannot say
public:
    enum class State { Stopped, Starting, Ready, Crashed, NotInstalled };
    static constexpr int CancelledCode = -32800;       // the error a cancelled request is answered with
    explicit WorkerSupervisor(const QString &name, QObject *parent = nullptr);
    ~WorkerSupervisor() override;

    void start();
    void stop();
    void setAutoStart(bool on) { m_autoStart = on; }   // start on first request instead of eagerly
    int request(const QString &method, const QJsonObject &params = {}); // returns request id
    State state() const { return m_state; }
    QString stateName() const;
    Q_INVOKABLE void restart();                          // a person can ask for this from Settings
    Q_INVOKABLE void cancel(int id);                     // answered with CancelledCode if the worker stops in time
    QString name() const { return m_name; }
    QString status() const;
    QString pythonPath() const { return m_python; }
    int restarts() const { return m_restarts; }
    QStringList missing() const { return m_missing; }
    QStringList missingOptional() const { return m_missingOptional; }
    QString lastError() const { return m_lastError; }
    bool busy() const;
    QString activity() const;
    qreal progress() const;

    static QString workersDir();
    static QString findPython();

signals:
    void stateChanged();
    void activityChanged();
    void response(int id, const QJsonObject &result, const QJsonObject &error);
    void notification(const QString &method, const QJsonObject &params);
    void progressed(int id, const QString &stage, double fraction, const QString &text);

private:
    void setState(State s);
    void onNewConnection();
    void onReadyRead();
    void onProcessFinished(int code, QProcess::ExitStatus status);
    void scheduleRestart();
    void began(int id, const QString &method);
    void ended(int id);
    void failEverything(const QString &message, int code);

    QString m_name;
    QString m_python;
    QString m_socketPath;
    QLocalServer m_server;
    QPointer<QLocalSocket> m_conn;
    QProcess m_proc;
    QTimer *m_restartTimer = nullptr;
    JsonLineBuffer m_rx;
    State m_state = State::Stopped;
    int m_nextId = 1;
    int m_restarts = 0;
    int m_backoffMs = 1000;
    bool m_wantRunning = false;
    bool m_autoStart = false;
    struct Queued { int id; QString method; QJsonObject params; };
    QList<Queued> m_queue;
    QSet<int> m_outstanding;      // written to the socket, not yet answered

    struct Job { QString method, stage, text; double fraction = -1; QElapsedTimer age; };
    QMap<int, Job> m_jobs;        // every request not yet answered, queued or written
    QTimer *m_busyTimer = nullptr;
    int m_lastProgressId = 0;
    QStringList m_missing, m_missingOptional, m_stderrTail;
    QString m_lastError;
};
