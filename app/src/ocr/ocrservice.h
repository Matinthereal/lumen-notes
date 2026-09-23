#pragma once
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QSet>
#include <QTimer>
#include <QVariantList>
#include <functional>

class Database;
class Library;
class WorkerSupervisor;

// Background handwriting OCR (idle-time, niced worker) → ocr_result rows + FTS; lasso → LaTeX.
class OcrService : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool ready READ ready NOTIFY stateChanged)
    Q_PROPERTY(QString status READ status NOTIFY stateChanged)
    Q_PROPERTY(int queued READ queued NOTIFY stateChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)            // reading a page or loading the model
    Q_PROPERTY(bool latexBusy READ latexBusy NOTIFY stateChanged)  // reading lasso'd maths
    Q_PROPERTY(qreal progress READ progress NOTIFY stateChanged)   // 0..1 through the page, -1 unknown
public:
    OcrService(Database &db, Library &lib, WorkerSupervisor *worker, QObject *parent = nullptr);
    bool ready() const { return m_ready; }
    QString status() const { return m_status; }
    int queued() const { return m_queue.size(); }
    bool busy() const { return m_running || m_preparing; }
    bool latexBusy() const { return m_latexId != 0; }
    qreal progress() const { return m_progress; }

    Q_INVOKABLE void prepare();
    Q_INVOKABLE void schedule(qint64 pageId);            // after a save; debounced, idle-time
    Q_INVOKABLE void recognizeNow(qint64 pageId);
    Q_INVOKABLE QVariantList results(qint64 pageId) const;
    Q_INVOKABLE void correct(qint64 resultId, const QString &text);
    Q_INVOKABLE void latexFromImage(const QString &png);  // → latexReady()
    Q_INVOKABLE void scanStale(int limit = 20);           // pages whose ink is newer than their OCR
    Q_INVOKABLE void cancel();                            // stop reading handwriting (the rest of the queue too)
    Q_INVOKABLE void cancelLatex();

signals:
    void stateChanged();
    void pageRecognized(qint64 pageId, int lines);
    void latexReady(const QString &latex, const QString &png);
    void failed(const QString &message);

private:
    using Callback = std::function<void(const QJsonObject &, const QJsonObject &)>;
    int call(const QString &method, const QJsonObject &params, Callback cb);
    void runNext();
    void recognizePage(qint64 pageId);
    void setStatus(const QString &s);
    bool notInstalled(const QJsonObject &error) const;
    static QString addOnHint();

    Database &m_db;
    Library &m_lib;
    WorkerSupervisor *m_worker;
    QHash<int, Callback> m_pending;
    QSet<qint64> m_queue;
    QTimer m_debounce, m_idleUnload;
    bool m_ready = false, m_running = false, m_preparing = false;
    int m_pageRequest = 0, m_latexId = 0;
    qreal m_progress = -1;
    QString m_status;
};
