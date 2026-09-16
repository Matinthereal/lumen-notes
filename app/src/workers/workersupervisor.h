#pragma once
#include <QJsonObject>
#include <QLocalServer>
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
class WorkerSupervisor : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString state READ stateName NOTIFY stateChanged)
    Q_PROPERTY(QString python READ pythonPath CONSTANT)
    Q_PROPERTY(int restarts READ restarts NOTIFY stateChanged)
public:
    enum class State { Stopped, Starting, Ready, Crashed };
    explicit WorkerSupervisor(const QString &name, QObject *parent = nullptr);
    ~WorkerSupervisor() override;

    void start();
    void stop();
    void setAutoStart(bool on) { m_autoStart = on; }   // start on first request instead of eagerly
    int request(const QString &method, const QJsonObject &params = {}); // returns request id
    State state() const { return m_state; }
    QString stateName() const;
    Q_INVOKABLE void restart() { stop(); start(); }      // a person can ask for this from Settings
    QString pythonPath() const { return m_python; }
    int restarts() const { return m_restarts; }

    static QString workersDir();
    static QString findPython();

signals:
    void stateChanged();
    void response(int id, const QJsonObject &result, const QJsonObject &error);
    void notification(const QString &method, const QJsonObject &params);

private:
    void setState(State s);
    void onNewConnection();
    void onReadyRead();
    void onProcessFinished(int code, QProcess::ExitStatus status);
    void scheduleRestart();

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
};
