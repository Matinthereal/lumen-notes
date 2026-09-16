#pragma once
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QUrl>
#include <QVariantList>
#include <functional>

class Database;
class Library;
class WorkerSupervisor;

// Front door to the `pdf` worker: import (as a section of slides or as a page background), page
// renders for the canvas (cached PNGs), word boxes for text selection, flattened export. Every
// call is asynchronous; results come back as signals (D-006).
class PdfService : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString workerState READ workerState NOTIFY busyChanged)
public:
    PdfService(Database &db, Library &lib, WorkerSupervisor *worker, QObject *parent = nullptr);

    Q_INVOKABLE void importAsSection(const QUrl &file, qint64 notebookId, const QString &sectionName, const QString &token = {});
    Q_INVOKABLE void importAsBackground(const QUrl &file, qint64 pageId, int index = 0);
    Q_INVOKABLE void requestRender(qint64 pageId, qreal scale);
    Q_INVOKABLE void requestWords(qint64 pageId);
    Q_INVOKABLE void exportPages(const QString &kind, qint64 id, const QUrl &dest);
    Q_INVOKABLE bool pageHasPdf(qint64 pageId) const;
    Q_INVOKABLE void prerender(qint64 pageId, qreal scale);   // warm the cache, no signal
    bool busy() const { return !m_pending.isEmpty(); }
    QString workerState() const;

signals:
    void rendered(qint64 pageId, qreal scale, const QString &file, int width, int height);
    void words(qint64 pageId, const QVariantList &words);
    void imported(qint64 sectionId, qint64 firstPageId, const QString &token);
    void exported(const QString &file, int pages);
    void failed(const QString &message);
    void busyChanged();

private:
    using Callback = std::function<void(const QJsonObject &result, const QJsonObject &error)>;
    void call(const QString &method, const QJsonObject &params, Callback cb);
    struct PdfRef { QString path; int index = -1; };
    PdfRef refFor(qint64 pageId) const;
    QJsonObject pagePayload(qint64 pageId) const;   // src/index/size/polys for export

    Database &m_db;
    Library &m_lib;
    WorkerSupervisor *m_worker;
    QHash<int, Callback> m_pending;
};
