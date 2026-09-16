#include "pdfservice.h"
#include "ink/tessellate.h"
#include "storage/attachments.h"
#include "storage/database.h"
#include "storage/library.h"
#include "storage/paths.h"
#include "storage/strokecodec.h"
#include "workers/workersupervisor.h"
#include <QFileInfo>
#include <QJsonArray>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(lcPdf, "lumen.pdf")

PdfService::PdfService(Database &db, Library &lib, WorkerSupervisor *worker, QObject *parent)
    : QObject(parent), m_db(db), m_lib(lib), m_worker(worker)
{
    connect(worker, &WorkerSupervisor::response, this, [this](int id, const QJsonObject &result, const QJsonObject &error) {
        const Callback cb = m_pending.take(id);
        if (cb) cb(result, error);
        emit busyChanged();
    });
    connect(worker, &WorkerSupervisor::stateChanged, this, &PdfService::busyChanged);
}

QString PdfService::workerState() const { return m_worker->stateName(); }

void PdfService::call(const QString &method, const QJsonObject &params, Callback cb)
{
    const int id = m_worker->request(method, params);
    m_pending.insert(id, std::move(cb));
    emit busyChanged();
}

PdfService::PdfRef PdfService::refFor(qint64 pageId) const
{
    Database::Query q(m_db, "SELECT attachment, page_index FROM pdf_page WHERE page_id=?");
    q.bind(1, pageId);
    if (!q.step()) return {};
    return {attachments::pathFor(q.text(0), "application/pdf"), q.i32(1)};
}

bool PdfService::pageHasPdf(qint64 pageId) const { return refFor(pageId).index >= 0; }

void PdfService::importAsSection(const QUrl &file, qint64 notebookId, const QString &sectionName, const QString &token)
{
    QString err;
    const QString sha = attachments::store(m_db, file.toLocalFile(), "application/pdf", &err);
    if (sha.isEmpty()) { emit failed(err); return; }
    const QString path = attachments::pathFor(sha, "application/pdf");
    const QString name = sectionName.isEmpty() ? QFileInfo(file.toLocalFile()).completeBaseName() : sectionName;
    call("info", {{"path", path}}, [this, sha, path, notebookId, name, token](const QJsonObject &r, const QJsonObject &e) {
        if (!e.isEmpty()) { emit failed(e.value("message").toString()); return; }
        const int pages = r.value("pages").toInt();
        const QJsonArray sizes = r.value("sizes").toArray();
        const qint64 sectionId = m_lib.createSection(notebookId, name, "slides");
        qint64 first = 0;
        m_db.begin();
        for (int i = 0; i < pages; ++i) {
            const qint64 pid = m_lib.createPage(sectionId, "plain", "a4");
            if (!first) first = pid;
            const QJsonArray sz = sizes.at(i).toArray();
            m_lib.setPageSize(pid, sz.at(0).toDouble(794), sz.at(1).toDouble(1123));
            m_lib.setPagePdf(pid, sha, i);
        }
        m_db.commit();
        // Index the text of every page for Ctrl+K, one request per page, in the background.
        for (int i = 0; i < pages; ++i) {
            Database::Query q(m_db, "SELECT page_id FROM pdf_page WHERE attachment=? AND page_index=?");
            q.bind(1, sha).bind(2, i);
            if (!q.step()) continue;
            const qint64 pid = q.i64(0);
            call("text", {{"path", path}, {"index", i}}, [this, pid](const QJsonObject &r, const QJsonObject &) {
                m_lib.indexText("pdf", pid, 0, r.value("text").toString());
            });
        }
        emit imported(sectionId, first, token);
    });
}

void PdfService::importAsBackground(const QUrl &file, qint64 pageId, int index)
{
    QString err;
    const QString sha = attachments::store(m_db, file.toLocalFile(), "application/pdf", &err);
    if (sha.isEmpty()) { emit failed(err); return; }
    const QString path = attachments::pathFor(sha, "application/pdf");
    call("info", {{"path", path}}, [this, sha, path, pageId, index](const QJsonObject &r, const QJsonObject &e) {
        if (!e.isEmpty()) { emit failed(e.value("message").toString()); return; }
        const QJsonArray sz = r.value("sizes").toArray().at(index).toArray();
        m_lib.setPageSize(pageId, sz.at(0).toDouble(794), sz.at(1).toDouble(1123));
        m_lib.setPagePdf(pageId, sha, index);
        m_lib.setPageStyle(pageId, "plain");
        call("text", {{"path", path}, {"index", index}}, [this, pageId](const QJsonObject &r, const QJsonObject &) {
            m_lib.indexText("pdf", pageId, 0, r.value("text").toString());
        });
        emit imported(0, pageId, QString());
    });
}

void PdfService::requestRender(qint64 pageId, qreal scale)
{
    const PdfRef ref = refFor(pageId);
    if (ref.index < 0) return;
    call("render", {{"path", ref.path}, {"index", ref.index}, {"scale", scale}, {"out_dir", paths::cacheDir() + "/pdf"}},
         [this, pageId](const QJsonObject &r, const QJsonObject &e) {
             if (!e.isEmpty()) { emit failed(e.value("message").toString()); return; }
             emit rendered(pageId, r.value("scale").toDouble(), r.value("file").toString(), r.value("width").toInt(), r.value("height").toInt());
         });
}

void PdfService::prerender(qint64 pageId, qreal scale)
{
    const PdfRef ref = refFor(pageId);
    if (ref.index < 0) return;
    call("render", {{"path", ref.path}, {"index", ref.index}, {"scale", scale}, {"out_dir", paths::cacheDir() + "/pdf"}}, [](const QJsonObject &, const QJsonObject &) {});
}

void PdfService::requestWords(qint64 pageId)
{
    const PdfRef ref = refFor(pageId);
    if (ref.index < 0) { emit words(pageId, {}); return; }
    call("words", {{"path", ref.path}, {"index", ref.index}}, [this, pageId](const QJsonObject &r, const QJsonObject &) {
        emit words(pageId, r.value("words").toArray().toVariantList());
    });
}

QJsonObject PdfService::pagePayload(qint64 pageId) const
{
    const QVariantMap info = m_lib.page(pageId);
    QJsonObject pg{{"width", info.value("width").toDouble()}, {"height", info.value("height").toDouble()}};
    const PdfRef ref = refFor(pageId);
    if (ref.index >= 0) { pg.insert("src", ref.path); pg.insert("index", ref.index); }
    QJsonArray polys;
    Database::Query q(m_db, "SELECT data FROM stroke_blob WHERE page_id=?");
    q.bind(1, pageId);
    if (q.step()) {
        QVector<Stroke> strokes;
        if (strokecodec::decode(q.blob(0), strokes)) {
            const PressureCurve curve = PressureCurve::forStyle(PenStyle::Classic);
            for (const Stroke &s : strokes) {
                QJsonArray pts;
                for (const InkPoint &p : smoothStroke(s.points, 2.f)) {
                    const float w = s.tool == InkTool::Highlighter ? s.width : curve.widthFor(s.width, p.pressure);
                    pts.append(QJsonArray{p.x, p.y, w});
                }
                const QColor c = QColor::fromRgba(s.color);
                polys.append(QJsonObject{{"points", pts}, {"color", QJsonArray{c.redF(), c.greenF(), c.blueF()}},
                                         {"opacity", c.alphaF()}, {"round", s.tool == InkTool::Pen}});
            }
        }
    }
    pg.insert("polys", polys);
    QJsonArray pics;
    {
        Database::Query im(m_db, "SELECT i.attachment, a.mime, i.x, i.y, i.w, i.h FROM image i JOIN attachment a ON a.sha256=i.attachment WHERE i.page_id=? ORDER BY i.id");
        im.bind(1, pageId);
        while (im.step())
            pics.append(QJsonObject{{"path", attachments::pathFor(im.text(0), im.text(1))},
                                    {"x", im.f64(2)}, {"y", im.f64(3)}, {"w", im.f64(4)}, {"h", im.f64(5)}});
    }
    pg.insert("images", pics);
    QJsonArray blocks;
    {   // typed blocks were missing from every export until now
        Database::Query tb(m_db, "SELECT x, y, w, markdown FROM text_block WHERE page_id=? ORDER BY sort, id");
        tb.bind(1, pageId);
        while (tb.step()) {
            const QString md = tb.text(3);
            if (md.trimmed().isEmpty()) continue;
            blocks.append(QJsonObject{{"x", tb.f64(0)}, {"y", tb.f64(1)}, {"w", tb.f64(2)}, {"markdown", md}});
        }
    }
    pg.insert("blocks", blocks);
    QJsonArray shapes;
    {
        Database::Query sq(m_db, "SELECT kind, x, y, w, h, stroke, fill, width FROM shape WHERE page_id=? ORDER BY sort, id");
        sq.bind(1, pageId);
        while (sq.step())
            shapes.append(QJsonObject{{"kind", sq.text(0)}, {"x", sq.f64(1)}, {"y", sq.f64(2)}, {"w", sq.f64(3)}, {"h", sq.f64(4)},
                                      {"stroke", sq.text(5)}, {"fill", sq.text(6)}, {"width", sq.f64(7)}});
    }
    pg.insert("shapes", shapes);
    return pg;
}

void PdfService::exportPages(const QString &kind, qint64 id, const QUrl &dest)
{
    QVector<qint64> pageIds;
    if (kind == "page") pageIds.append(id);
    else if (kind == "section") { for (const QVariant &p : m_lib.pages(id)) pageIds.append(p.toMap().value("id").toLongLong()); }
    else if (kind == "notebook") {
        for (const QVariant &s : m_lib.sections(id))
            for (const QVariant &p : m_lib.pages(s.toMap().value("id").toLongLong())) pageIds.append(p.toMap().value("id").toLongLong());
    }
    if (pageIds.isEmpty()) { emit failed("nothing to export"); return; }
    QJsonArray pages;
    for (qint64 pid : pageIds) pages.append(pagePayload(pid));
    call("export", {{"pages", pages}, {"out", dest.toLocalFile()}}, [this](const QJsonObject &r, const QJsonObject &e) {
        if (!e.isEmpty()) { emit failed(e.value("message").toString()); return; }
        emit exported(r.value("file").toString(), r.value("pages").toInt());
    });
}
