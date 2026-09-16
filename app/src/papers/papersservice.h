#pragma once
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QRectF>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>
#include <functional>

class Database;
class Library;
class PdfService;
class WorkerSupervisor;

// Past papers (Phase 9): a paper + mark-scheme pair, questions as regions on pages, marks per
// question per attempt, topic tags, timer mode, and the weak-topics dashboard.
class PapersService : public QObject {
    Q_OBJECT
    Q_PROPERTY(qint64 activeAttempt READ activeAttempt NOTIFY attemptChanged)
    Q_PROPERTY(qint64 activePaper READ activePaper NOTIFY attemptChanged)
public:
    PapersService(Database &db, Library &lib, PdfService &pdf, WorkerSupervisor *pdfWorker, QObject *parent = nullptr);

    Q_INVOKABLE void importPair(const QUrl &paperPdf, const QUrl &schemePdf, const QString &subject, int year, const QString &name, const QString &board, int minutes);
    Q_INVOKABLE QVariantList subjects() const;
    Q_INVOKABLE QVariantList papers(const QString &subject = {}) const;      // Subject → Year → Paper (board as a tag)
    Q_INVOKABLE QVariantMap paper(qint64 id) const;
    Q_INVOKABLE qint64 paperForPage(qint64 pageId) const;
    Q_INVOKABLE void removePaper(qint64 id);     // soft: restorePaper() puts it back
    Q_INVOKABLE void restorePaper(qint64 id);
    Q_INVOKABLE QVariantList questions(qint64 paperId) const;
    Q_INVOKABLE qint64 addQuestion(qint64 paperId, const QString &label, int pageIndex, const QRectF &rect, int marks);
    Q_INVOKABLE void setQuestion(qint64 questionId, const QString &label, int marks, const QString &topics);
    Q_INVOKABLE void setRegion(qint64 questionId, int pageIndex, const QRectF &rect);
    Q_INVOKABLE void removeQuestion(qint64 questionId);
    Q_INVOKABLE void detectQuestions(qint64 paperId);                          // from the PDF text → questionsDetected()

    Q_INVOKABLE qint64 startAttempt(qint64 paperId, bool timerMode);
    Q_INVOKABLE void recordAnswer(qint64 attemptId, qint64 questionId, int marksScored, int seconds, const QString &errorType);
    Q_INVOKABLE void finishAttempt(qint64 attemptId);
    Q_INVOKABLE QVariantList attempts(qint64 paperId) const;
    Q_INVOKABLE QVariantList attemptResults(qint64 attemptId) const;
    Q_INVOKABLE QVariantMap attemptSummary(qint64 attemptId) const;
    qint64 activeAttempt() const { return m_activeAttempt; }
    qint64 activePaper() const { return m_activePaper; }
    Q_INVOKABLE void setActive(qint64 paperId, qint64 attemptId) { m_activePaper = paperId; m_activeAttempt = attemptId; emit attemptChanged(); }

    Q_INVOKABLE QVariantList weakTopics(const QString &subject, int days = 365) const;    // topic → %, attempts
    Q_INVOKABLE QVariantList errorBreakdown(const QString &subject, int days = 365) const;
    Q_INVOKABLE QVariantList trend(const QString &subject, int days = 365) const;         // attempt → %

signals:
    void imported(qint64 paperId, qint64 firstPageId);
    void questionsDetected(qint64 paperId, int count);
    void attemptChanged();
    void changed();
    void failed(const QString &message);

private:
    using Callback = std::function<void(const QJsonObject &, const QJsonObject &)>;
    void call(const QString &method, const QJsonObject &params, Callback cb);
    Database &m_db;
    Library &m_lib;
    PdfService &m_pdf;
    WorkerSupervisor *m_worker;
    QHash<int, Callback> m_pending;
    qint64 m_activeAttempt = 0, m_activePaper = 0;
};
