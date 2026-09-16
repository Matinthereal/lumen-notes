#include "pingmeter.h"
#include "workersupervisor.h"
#include <QJsonObject>
#include <QTimer>

PingMeter::PingMeter(WorkerSupervisor *worker, QObject *parent)
    : QObject(parent), m_worker(worker), m_timer(new QTimer(this))
{
    m_timer->setInterval(2000);
    connect(m_timer, &QTimer::timeout, this, &PingMeter::tick);
    connect(m_worker, &WorkerSupervisor::response, this, [this](int id, const QJsonObject &result, const QJsonObject &error) {
        if (id != m_pendingId) return;
        m_pendingId = -1;
        if (!error.isEmpty()) {
            m_lastError = error.value("message").toString();
        } else {
            m_rttMs = m_clock.nsecsElapsed() / 1e6;
            ++m_pongs;
            m_workerPython = result.value("python").toString();
            m_lastError.clear();
        }
        emit changed();
    });
    connect(m_worker, &WorkerSupervisor::stateChanged, this, [this] {
        if (m_worker->state() == WorkerSupervisor::State::Ready) tick();
    });
    m_timer->start();
}

void PingMeter::tick()
{
    if (m_pendingId != -1 || m_worker->state() != WorkerSupervisor::State::Ready) return;
    m_clock.start();
    m_pendingId = m_worker->request(QStringLiteral("ping"));
}
