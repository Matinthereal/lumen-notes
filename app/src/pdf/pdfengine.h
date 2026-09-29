#pragma once
#include "workers/inprocessworker.h"
#include <QHash>
#include <QStringList>

struct fpdf_document_t__;

// The `pdf` helper (workers/lumen_workers/pdf.py) on PDFium, in-process: same methods, same JSON,
// coordinates in PAGE UNITS (1/96 in). PDFium is not thread-safe, so one instance is only ever
// called from one thread at a time (WorkerSupervisor gives it a thread of its own).
class PdfEngine : public InProcessWorker {
public:
    PdfEngine();
    ~PdfEngine() override;
    QJsonObject call(const QString &method, const QJsonObject &params, QString *error) override;

private:
    fpdf_document_t__ *document(const QString &path, QString *error);
    QJsonObject info(const QJsonObject &p, QString *error);
    QJsonObject render(const QJsonObject &p, QString *error);
    QJsonObject words(const QJsonObject &p, QString *error);
    QJsonObject text(const QJsonObject &p, QString *error);
    QJsonObject search(const QJsonObject &p, QString *error);
    QJsonObject exportPages(const QJsonObject &p, QString *error);
    QJsonObject makeTestPdf(const QJsonObject &p, QString *error);

    QHash<QString, fpdf_document_t__ *> m_docs;
    QStringList m_recent;       // least recently used first; a term of slide decks must not stay open
};
