#include "workersupervisor.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocalSocket>
#include <QLoggingCategory>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTimer>

Q_LOGGING_CATEGORY(lcWorker, "lumen.worker")

QString WorkerSupervisor::workersDir()
{
    const QByteArray env = qgetenv("LUMEN_WORKERS_DIR");
    if (!env.isEmpty()) return QString::fromLocal8Bit(env);
#ifdef Q_OS_WIN
    // Windows installs are flat: workers/ sits next to lumen.exe, not under a bin/ + share/ split.
    const QString installed = QCoreApplication::applicationDirPath() + QStringLiteral("/workers");
#else
    // Installed layout: <prefix>/share/lumen/workers next to <prefix>/bin/lumen; else the source tree.
    const QString installed = QCoreApplication::applicationDirPath() + QStringLiteral("/../share/lumen/workers");
#endif
    if (QFileInfo::exists(installed + "/lumen_workers/rpc.py")) return QDir::cleanPath(installed);
    return QStringLiteral(LUMEN_WORKERS_SOURCE_DIR);
}

QString WorkerSupervisor::findPython()
{
    // The venv's own path, never the resolved symlink (that lands on the bare interpreter). The
    // repo venv first, then one under the data dir for installed builds, then python3 on PATH.
    // venv layout is bin/python on Unix, Scripts/python.exe on Windows (the interpreter that
    // created the venv decides this, not the platform running this code).
#ifdef Q_OS_WIN
    const QString venvPython = QStringLiteral("Scripts/python.exe");
    const QString sysPython = QStringLiteral("python");
#else
    const QString venvPython = QStringLiteral("bin/python");
    const QString sysPython = QStringLiteral("python3");
#endif
    for (const QString &venv : {QDir(workersDir()).filePath("../.venv/" + venvPython),
                                QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/lumen/venv/" + venvPython})
        if (QFileInfo::exists(venv))
            return QDir::cleanPath(QFileInfo(venv).absoluteFilePath());
    const QString sys = QStandardPaths::findExecutable(sysPython);
    return sys.isEmpty() ? sysPython : sys;
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
            if (!line.trimmed().isEmpty()) {
                qCInfo(lcWorker) << m_name << "stderr:" << line;
                m_stderrTail << QString::fromUtf8(line.trimmed());
                if (m_stderrTail.size() > 30) m_stderrTail.removeFirst();
            }
    });
    connect(&m_proc, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e != QProcess::FailedToStart) return;
        // No Python at all: that is a missing install, and retrying will not conjure one up.
        m_missing = {QStringLiteral("python3")};
        m_lastError = m_proc.errorString();
        failEverything(QStringLiteral("%1 helper cannot start: Python was not found").arg(m_name), -4);
        setState(State::NotInstalled);
    });
    m_proc.setWorkingDirectory(workersDir());

    // A request counts as keeping the worker busy once it has taken longer than a person notices
    // (the Doherty threshold); the ping every two seconds never does.
    m_busyTimer = new QTimer(this);
    m_busyTimer->setSingleShot(true);
    m_busyTimer->setInterval(400);
    connect(m_busyTimer, &QTimer::timeout, this, &WorkerSupervisor::activityChanged);
}

WorkerSupervisor::~WorkerSupervisor() { stop(); }

QString WorkerSupervisor::stateName() const
{
    switch (m_state) {
    case State::Stopped: return QStringLiteral("stopped");
    case State::Starting: return QStringLiteral("starting");
    case State::Ready: return QStringLiteral("ready");
    case State::Crashed: return QStringLiteral("crashed");
    case State::NotInstalled: return QStringLiteral("not installed");
    }
    return {};
}

QString WorkerSupervisor::status() const
{
    if (m_state == State::NotInstalled || m_state == State::Crashed || m_state == State::Starting) return stateName();
    if (m_state == State::Ready) {
        for (const Job &j : m_jobs)
            if (j.method == QLatin1String("prepare") || j.stage == QLatin1String("loading") || j.stage == QLatin1String("downloading"))
                return QStringLiteral("loading model");
        if (busy()) return QStringLiteral("busy");
    }
    if (!m_missing.isEmpty()) return QStringLiteral("not installed");      // it runs, but can do nothing useful
    return stateName();
}

void WorkerSupervisor::check()
{
    if (m_state == State::Ready || m_state == State::Starting) return;      // it has said, or is about to
    m_stopAfterHello = !m_wantRunning || m_state == State::NotInstalled;
    if (m_state == State::NotInstalled) restart(); else start();
}

bool WorkerSupervisor::busy() const
{
    for (const Job &j : m_jobs)
        if (!j.stage.isEmpty() || (j.age.isValid() && j.age.elapsed() >= m_busyTimer->interval())) return true;
    return false;
}

QString WorkerSupervisor::activity() const
{
    const auto it = m_jobs.constFind(m_lastProgressId);
    if (it != m_jobs.cend() && !it->text.isEmpty()) return it->text;
    return busy() && !m_jobs.isEmpty() ? m_jobs.last().method : QString();
}

qreal WorkerSupervisor::progress() const
{
    const auto it = m_jobs.constFind(m_lastProgressId);
    return it != m_jobs.cend() ? it->fraction : -1;
}

void WorkerSupervisor::began(int id, const QString &method)
{
    Job j;
    j.method = method;
    j.age.start();
    m_jobs.insert(id, j);
    m_busyTimer->start();
}

void WorkerSupervisor::ended(int id)
{
    if (m_jobs.remove(id)) emit activityChanged();
}

void WorkerSupervisor::failEverything(const QString &message, int code)
{
    const QSet<int> gone = std::move(m_outstanding); m_outstanding.clear();
    QList<int> ids(gone.cbegin(), gone.cend());
    for (const Queued &q : std::as_const(m_queue)) ids << q.id;
    m_queue.clear();
    m_jobs.clear();
    emit activityChanged();
    for (int id : ids) emit response(id, {}, QJsonObject{{"message", message}, {"code", code}});
}

void WorkerSupervisor::restart()
{
    stop();
    m_missing.clear();
    m_missingOptional.clear();
    m_lastError.clear();
    m_stderrTail.clear();
    start();
}

void WorkerSupervisor::cancel(int id)
{
    for (int i = 0; i < m_queue.size(); ++i) {
        if (m_queue[i].id != id) continue;          // never sent: just drop it
        m_queue.removeAt(i);
        ended(id);
        QTimer::singleShot(0, this, [this, id] { emit response(id, {}, QJsonObject{{"message", "cancelled"}, {"code", CancelledCode}}); });
        return;
    }
    if (m_conn && m_outstanding.contains(id))
        m_conn->write(JsonLineBuffer::encode({{"method", "cancel"}, {"params", QJsonObject{{"id", id}}}}));
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
    m_stderrTail.clear();
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
    if (m_state == State::NotInstalled) {
        // Starting it again would die on the same import: say so at once, like any other failure.
        const QString why = QStringLiteral("the %1 helper is not installed (missing %2)").arg(m_name, m_missing.join(QStringLiteral(", ")));
        QTimer::singleShot(0, this, [this, id, why] { emit response(id, {}, QJsonObject{{"message", why}, {"code", -4}}); });
        return id;
    }
    began(id, method);
    if (!m_conn || m_state != State::Ready) {
        if (m_autoStart || m_wantRunning) {          // starting, or a restart is pending: queue
            if (m_proc.state() == QProcess::NotRunning && !m_restartTimer->isActive()) start();
            m_queue.append({id, method, params});          // sent the moment the worker connects
            if (m_queue.size() > 200) { const Queued q = m_queue.takeFirst(); ended(q.id); QTimer::singleShot(0, this, [this, q] { emit response(q.id, {}, QJsonObject{{"message", "worker queue overflow"}, {"code", -2}}); }); }
            return id;
        }
        // Answer asynchronously so callers never special-case "not ready".
        ended(id);
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
            const int id = msg.value("id").toInt();
            m_outstanding.remove(id);
            ended(id);
            emit response(id, msg.value("result").toObject(), msg.value("error").toObject());
        } else if (msg.contains("method")) {
            const QString method = msg.value("method").toString();
            const QJsonObject params = msg.value("params").toObject();
            if (method == QLatin1String("progress")) {
                const int id = params.value("id").toInt();
                const double fraction = params.contains("fraction") ? params.value("fraction").toDouble() : -1;
                auto it = m_jobs.find(id);
                if (it != m_jobs.end()) {
                    it->stage = params.value("stage").toString();
                    it->text = params.value("text").toString();
                    it->fraction = fraction;
                    m_lastProgressId = id;
                    emit activityChanged();
                }
                emit progressed(id, params.value("stage").toString(), fraction, params.value("text").toString());
                continue;
            }
            if (method == QLatin1String("hello")) {
                auto list = [](const QJsonValue &v) { QStringList out; for (const QJsonValue &x : v.toArray()) out << x.toString(); return out; };
                m_missing = list(params.value("missing"));
                m_missingOptional = list(params.value("missing_optional"));
                emit stateChanged();
                emit activityChanged();
                if (m_stopAfterHello) {
                    m_stopAfterHello = false;
                    QTimer::singleShot(0, this, [this] { if (m_jobs.isEmpty() && m_queue.isEmpty()) stop(); });
                }
            }
            emit notification(method, params);
        }
    }
}

void WorkerSupervisor::onProcessFinished(int code, QProcess::ExitStatus status)
{
    qCInfo(lcWorker) << m_name << "exited" << code << (status == QProcess::CrashExit ? "crash" : "normal");
    if (m_conn) { m_conn->deleteLater(); m_conn = nullptr; }
    for (const QByteArray &line : m_proc.readAllStandardError().split('\n'))      // what arrived with the exit
        if (!line.trimmed().isEmpty()) m_stderrTail << QString::fromUtf8(line.trimmed());
    if (!m_stderrTail.isEmpty()) m_lastError = m_stderrTail.last();
    // Died on an import: a package is missing, and restarting will only die the same way.
    static const QRegularExpression noModule(QStringLiteral("(?:ModuleNotFoundError|ImportError): No module named '([^']+)'"));
    for (auto it = m_stderrTail.crbegin(); m_wantRunning && it != m_stderrTail.crend(); ++it) {
        const QRegularExpressionMatch m = noModule.match(*it);
        if (!m.hasMatch()) continue;
        m_missing = {m.captured(1).section(QLatin1Char('.'), 0, 0)};
        m_stderrTail.clear();
        failEverything(QStringLiteral("the %1 helper is not installed (missing %2)").arg(m_name, m_missing.first()), -4);
        setState(State::NotInstalled);
        return;
    }
    // Anything the worker had in hand dies with it: fail those requests so no caller waits forever.
    const QSet<int> gone = std::move(m_outstanding); m_outstanding.clear();
    for (int id : gone) { ended(id); emit response(id, {}, QJsonObject{{"message", QStringLiteral("%1 worker exited before answering").arg(m_name)}, {"code", -3}}); }
    if (!m_queue.isEmpty() && !m_wantRunning) {
        const QList<Queued> lost = std::move(m_queue); m_queue.clear();
        for (const Queued &q : lost) { ended(q.id); emit response(q.id, {}, QJsonObject{{"message", "worker exited before answering"}, {"code", -3}}); }
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
