#pragma once
#include <QObject>
#include <QStringList>
#include <QTimer>

class WorkerSupervisor;

// Apple's Math Notes, in our idiom: write a sum, ask for the answer. The worker does the algebra
// (sympy); this keeps the page's variable definitions so `v = 3` earlier on the page makes `2v`
// worth 6, and hands answers back as both plain text and LaTeX so they render like any formula.
class MathsService : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString lastResult READ lastResult NOTIFY solved)
    Q_PROPERTY(QString lastLatex READ lastLatex NOTIFY solved)
public:
    MathsService(WorkerSupervisor &worker, QObject *parent = nullptr);

    Q_INVOKABLE void solve(const QString &expression, const QStringList &context = {});
    Q_INVOKABLE void variables(const QStringList &lines);
    bool busy() const { return m_pending != 0; }
    QString lastResult() const { return m_result; }
    QString lastLatex() const { return m_latex; }

signals:
    void solved(const QString &input, const QString &result, const QString &latex, const QString &kind);
    void failed(const QString &reason);
    void variablesFound(const QVariantMap &variables);
    void busyChanged();

private:
    void startPending(int id);

    WorkerSupervisor &m_worker;
    // sympy has no notion of giving up: `9^9^9^9` or a transcendental solve blocks the worker for
    // ever, and `busy` never cleared — leaving "Work it out" disabled until the app was restarted.
    QTimer m_timeout;
    int m_pending = 0;
    QString m_result;
    QString m_latex;
};
