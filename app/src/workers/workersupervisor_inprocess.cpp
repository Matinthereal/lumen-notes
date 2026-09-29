// WorkerSupervisor for builds without the Python helpers (see workersupervisor.h): the helpers
// that have an in-process implementation run on a thread; the rest are not on this device.
#include "workersupervisor.h"
#include "inprocessworker.h"
#include <QLoggingCategory>
#include <QThread>
#include <QTimer>
#ifdef LUMEN_HAVE_PDFIUM
#include "pdf/pdfengine.h"
#endif

Q_DECLARE_LOGGING_CATEGORY(lcWorker)

static std::unique_ptr<InProcessWorker> makeInProcess(const QString &name)
{
#ifdef LUMEN_HAVE_PDFIUM
    if (name == QLatin1String("pdf")) return std::make_unique<PdfEngine>();
#endif
    Q_UNUSED(name);
    return nullptr;
}

WorkerSupervisor::WorkerSupervisor(const QString &name, QObject *parent)
    : QObject(parent), m_local(makeInProcess(name)), m_name(name)
{
    if (!m_local) {
        m_lastError = QStringLiteral("not available on this device");
        m_state = State::NotInstalled;
    }
    m_busyTimer = new QTimer(this);
    m_busyTimer->setSingleShot(true);
    m_busyTimer->setInterval(400);
    connect(m_busyTimer, &QTimer::timeout, this, &WorkerSupervisor::activityChanged);
}

void WorkerSupervisor::start()
{
    m_wantRunning = true;
    if (!m_local || m_thread) return;
    m_thread = new QThread(this);
    m_thread->setObjectName(m_name);
    m_threadContext = new QObject;
    m_threadContext->moveToThread(m_thread);
    connect(m_thread, &QThread::finished, m_threadContext, &QObject::deleteLater);
    m_thread->start();
    qCInfo(lcWorker) << m_name << "running in-process";
    setState(State::Ready);
}

void WorkerSupervisor::stop()
{
    m_wantRunning = false;
    if (!m_thread) return;
    m_thread->quit();
    m_thread->wait();          // the call in hand finishes; queued ones are dropped with the thread
    delete m_thread;
    m_thread = nullptr;
    m_threadContext = nullptr;
    failEverything(QStringLiteral("%1 stopped before answering").arg(m_name), -3);
    setState(State::Stopped);
}

int WorkerSupervisor::request(const QString &method, const QJsonObject &params)
{
    const int id = m_nextId++;
    if (!m_local) {
        const QString why = QStringLiteral("%1 is not available on this device").arg(m_name);
        QTimer::singleShot(0, this, [this, id, why] { emit response(id, {}, QJsonObject{{"message", why}, {"code", -4}}); });
        return id;
    }
    if (!m_thread) {
        if (!m_autoStart && !m_wantRunning) {
            QTimer::singleShot(0, this, [this, id] { emit response(id, {}, QJsonObject{{"message", "worker not ready"}, {"code", -1}}); });
            return id;
        }
        start();
    }
    began(id, method);
    m_outstanding.insert(id);
    InProcessWorker *local = m_local.get();
    QMetaObject::invokeMethod(m_threadContext, [this, local, id, method, params] {
        QString error;
        const QJsonObject result = local->call(method, params, &error);
        QMetaObject::invokeMethod(this, [this, id, result, error] {
            if (!m_outstanding.remove(id)) return;          // already failed by stop()
            ended(id);
            if (error.isEmpty()) emit response(id, result, {});
            else emit response(id, {}, QJsonObject{{"message", error}, {"code", -5}});
        }, Qt::QueuedConnection);
    }, Qt::QueuedConnection);
    return id;
}

void WorkerSupervisor::check() { }

void WorkerSupervisor::cancel(int) { }      // in-process calls are short and cannot be interrupted
