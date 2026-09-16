#include "mathsservice.h"
#include "workers/workersupervisor.h"

#include <QJsonArray>
#include <QJsonObject>

MathsService::MathsService(WorkerSupervisor &worker, QObject *parent) : QObject(parent), m_worker(worker)
{
    connect(&m_worker, &WorkerSupervisor::response, this, [this](int id, const QJsonObject &result, const QJsonObject &error) {
        if (id != m_pending) return;
        m_pending = 0;
        m_timeout.stop();
        emit busyChanged();
        if (!error.isEmpty()) { emit failed(error.value("message").toString()); return; }
        if (result.contains("variables")) { emit variablesFound(result.value("variables").toObject().toVariantMap()); return; }
        if (!result.value("ok").toBool()) { emit failed(result.value("reason").toString()); return; }
        m_result = result.value("result").toString();
        m_latex = result.value("latex").toString();
        emit solved(result.value("input").toString(), m_result, m_latex, result.value("kind").toString());
    });
    m_timeout.setSingleShot(true);
    m_timeout.setInterval(20000);
    connect(&m_timeout, &QTimer::timeout, this, [this] {
        if (!m_pending) return;
        m_pending = 0;
        emit busyChanged();
        emit failed(tr("that took too long to work out — try a simpler expression"));
    });
}

void MathsService::startPending(int id)
{
    m_pending = id;
    m_timeout.start();
    emit busyChanged();
}

void MathsService::solve(const QString &expression, const QStringList &context)
{
    QJsonArray lines;
    for (const QString &line : context) lines.append(line);
    startPending(m_worker.request(QStringLiteral("solve"), {{"expression", expression}, {"context", lines}}));
}

void MathsService::variables(const QStringList &lines)
{
    QJsonArray arr;
    for (const QString &line : lines) arr.append(line);
    startPending(m_worker.request(QStringLiteral("define"), {{"lines", arr}}));
}
