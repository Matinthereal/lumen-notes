#include "workersupervisor.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QLocalSocket>
#include <QLoggingCategory>
#include <QStandardPaths>
#include <QTimer>

Q_LOGGING_CATEGORY(lcWorker, "lumen.worker")

QString WorkerSupervisor::workersDir()
{
    const QByteArray env = qgetenv("LUMEN_WORKERS_DIR");
    if (!env.isEmpty()) return QString::fromLocal8Bit(env);
    // Installed layout: <prefix>/share/lumen/workers next to <prefix>/bin/lumen; else the source tree.
    const QString installed = QCoreApplication::applicationDirPath() + QStringLiteral("/../share/lumen/workers");
    if (QFileInfo::exists(installed + "/lumen_workers/rpc.py")) return QDir::cleanPath(installed);
    return QStringLiteral(LUMEN_WORKERS_SOURCE_DIR);
}

QString WorkerSupervisor::findPython()
{
    // The venv's own path, never the resolved symlink (that lands on the bare interpreter). The
    // repo venv first, then one under the data dir for installed builds, then python3 on PATH.
    for (const QString &venv : {QDir(workersDir()).filePath("../.venv/bin/python"),
                                QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/lumen/venv/bin/python"})
        if (QFileInfo::exists(venv))
            return QDir::cleanPath(QFileInfo(venv).absoluteFilePath());
    const QString sys = QStandardPaths::findExecutable("python3");
    return sys.isEmpty() ? QStringLiteral("python3") : sys;
}

WorkerSupervisor::WorkerSupervisor(const QString &name, QObject *parent)
    : QObject(parent), m_name(name), m_python(findPython())
{
    const QString runtime = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    QDir(runtime).mkpath("lumen");
    m_socketPath = QDir(runtime).filePath(QStringLiteral("lumen/%1-%2.sock").arg(name).arg(QCoreApplication::applicationPid()));

    m_restartTimer = new QTimer(this);
    m_restartTimer->setSingleShot(true);
    connect(m_restartTimer, &QTimer::timeout, this, [this] { if (m_wantRunning) start(); });

    connect(&m_server, &QLocalServer::newConnection, this, &WorkerSupervisor::onNewConnection);
    connect(&m_proc, &QProcess::finished, this, &WorkerSupervisor::onProcessFinished);
    connect(&m_proc, &QProcess::readyReadStandardError, this, [this] {
        for (const QByteArray &line : m_proc.readAllStandardError().split('\n'))
            if (!line.trimmed().isEmpty())
                qCInfo(lcWorker) << m_name << "stderr:" << line;
    });
    m_proc.setWorkingDirectory(workersDir());
}

WorkerSupervisor::~WorkerSupervisor() { stop(); }

QString WorkerSupervisor::stateName() const
{
    switch (m_state) {
    case State::Stopped: return QStringLiteral("stopped");
    case State::Starting: return QStringLiteral("starting");
    case State::Ready: return QStringLiteral("ready");
    case State::Crashed: return QStringLiteral("crashed");
    }
    return {};
}

void WorkerSupervisor::setState(State s)
{
    if (m_state == s) return;
    m_state = s;
    emit stateChanged();
}

void WorkerSupervisor::start()
{
    m_wantRunning = true;
    if (m_proc.state() != QProcess::NotRunning)
        return;
    if (!m_server.isListening()) {
        // Only a server that is NOT listening may unlink a stale socket file: unlinking our own
        // live socket leaves us listening on a path nobody can connect to (caught by the tests).
        QLocalServer::removeServer(m_socketPath);
        if (!m_server.listen(m_socketPath)) {
            qCWarning(lcWorker) << "cannot listen on" << m_socketPath << m_server.errorString();
            setState(State::Crashed);
            scheduleRestart();
            return;
        }
    }
    setState(State::Starting);
    const QStringList args{QStringLiteral("-m"), QStringLiteral("lumen_workers.%1").arg(m_name),
                           QStringLiteral("--socket"), m_socketPath};
    qCInfo(lcWorker) << "starting" << m_python << args << "in" << m_proc.workingDirectory();
    m_proc.start(m_python, args);
}

void WorkerSupervisor::stop()
{
    m_wantRunning = false;
    m_restartTimer->stop();
    if (m_conn) {
        m_conn->write(JsonLineBuffer::encode({{"method", "shutdown"}}));
        m_conn->flush();
    }
    if (m_proc.state() != QProcess::NotRunning) {
        if (!m_proc.waitForFinished(m_name == QLatin1String("audio") ? 6000 : 1500)) {
            m_proc.kill();
            m_proc.waitForFinished(1000);
        }
    }
    m_server.close();
    QLocalServer::removeServer(m_socketPath);
    setState(State::Stopped);
}

int WorkerSupervisor::request(const QString &method, const QJsonObject &params)
{
    const int id = m_nextId++;
    if (!m_conn || m_state != State::Ready) {
        if (m_autoStart || m_wantRunning) {          // starting, or a restart is pending: queue
            if (m_proc.state() == QProcess::NotRunning && !m_restartTimer->isActive()) start();
            m_queue.append({id, method, params});          // sent the moment the worker connects
            if (m_queue.size() > 200) { const Queued q = m_queue.takeFirst(); QTimer::singleShot(0, this, [this, q] { emit response(q.id, {}, QJsonObject{{"message", "worker queue overflow"}, {"code", -2}}); }); }
            return id;
        }
        // Answer asynchronously so callers never special-case "not ready".
        QTimer::singleShot(0, this, [this, id] {
            emit response(id, {}, QJsonObject{{"message", "worker not ready"}, {"code", -1}});
        });
        return id;
    }
    m_outstanding.insert(id);
    m_conn->write(JsonLineBuffer::encode({{"id", id}, {"method", method}, {"params", params}}));
    return id;
}

void WorkerSupervisor::onNewConnection()
{
    QLocalSocket *sock = m_server.nextPendingConnection();
    if (!sock) return;
    if (m_conn) { m_conn->disconnectFromServer(); m_conn->deleteLater(); }
    m_conn = sock;
    m_rx = JsonLineBuffer();
    connect(sock, &QLocalSocket::readyRead, this, &WorkerSupervisor::onReadyRead);
    connect(sock, &QLocalSocket::disconnected, this, [this] {
        if (m_state == State::Ready) setState(State::Starting);
    });
    m_backoffMs = 1000;
    setState(State::Ready);
    qCInfo(lcWorker) << m_name << "connected";
    for (const Queued &q : std::as_const(m_queue)) { m_outstanding.insert(q.id); m_conn->write(JsonLineBuffer::encode({{"id", q.id}, {"method", q.method}, {"params", q.params}})); }
    m_queue.clear();
}

void WorkerSupervisor::onReadyRead()
{
    if (!m_conn) return;
    m_rx.append(m_conn->readAll());
    QJsonObject msg;
    while (m_rx.next(msg)) {
        if (msg.contains("id")) {
            m_outstanding.remove(msg.value("id").toInt());
            emit response(msg.value("id").toInt(), msg.value("result").toObject(), msg.value("error").toObject());
        } else if (msg.contains("method")) {
            emit notification(msg.value("method").toString(), msg.value("params").toObject());
        }
    }
}

void WorkerSupervisor::onProcessFinished(int code, QProcess::ExitStatus status)
{
    qCInfo(lcWorker) << m_name << "exited" << code << (status == QProcess::CrashExit ? "crash" : "normal");
    if (m_conn) { m_conn->deleteLater(); m_conn = nullptr; }
    // Anything the worker had in hand dies with it: fail those requests so no caller waits forever.
    const QSet<int> gone = std::move(m_outstanding); m_outstanding.clear();
    for (int id : gone) emit response(id, {}, QJsonObject{{"message", QStringLiteral("%1 worker exited before answering").arg(m_name)}, {"code", -3}});
    if (!m_queue.isEmpty() && !m_wantRunning) {
        const QList<Queued> lost = std::move(m_queue); m_queue.clear();
        for (const Queued &q : lost) emit response(q.id, {}, QJsonObject{{"message", "worker exited before answering"}, {"code", -3}});
    }
    if (m_wantRunning) {
        setState(State::Crashed);
        scheduleRestart();
    } else {
        setState(State::Stopped);
    }
}

void WorkerSupervisor::scheduleRestart()
{
    ++m_restarts;
    emit stateChanged();
    m_restartTimer->start(m_backoffMs);
    m_backoffMs = qMin(m_backoffMs * 2, 30000);
}
