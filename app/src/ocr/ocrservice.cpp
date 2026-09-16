#include "ocrservice.h"
#include "ink/tessellate.h"
#include "linesegment.h"
#include "storage/database.h"
#include "storage/library.h"
#include "storage/paths.h"
#include "storage/strokecodec.h"
#include "workers/workersupervisor.h"
#include <QDateTime>
#include <QDir>
#include <QImage>
#include <QJsonArray>
#include <QPainter>
#include <QPen>

OcrService::OcrService(Database &db, Library &lib, WorkerSupervisor *worker, QObject *parent)
    : QObject(parent), m_db(db), m_lib(lib), m_worker(worker)
{
    connect(worker, &WorkerSupervisor::response, this, [this](int id, const QJsonObject &r, const QJsonObject &e) {
        const Callback cb = m_pending.take(id);
        if (cb) cb(r, e);
    });
    connect(worker, &WorkerSupervisor::stateChanged, this, &OcrService::stateChanged);
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(10000);   // ten quiet seconds after the last save
    connect(&m_debounce, &QTimer::timeout, this, &OcrService::runNext);
    m_idleUnload.setSingleShot(true);
    m_idleUnload.setInterval(10 * 60 * 1000);   // models out of RAM after ten idle minutes
    connect(&m_idleUnload, &QTimer::timeout, this, [this] { if (m_queue.isEmpty() && !m_running && m_ready) { call("unload", {}, [](const QJsonObject &, const QJsonObject &) {}); m_ready = false; emit stateChanged(); } });
}

void OcrService::call(const QString &method, const QJsonObject &params, Callback cb) { m_pending.insert(m_worker->request(method, params), std::move(cb)); }
void OcrService::setStatus(const QString &s) { if (m_status == s) return; m_status = s; emit stateChanged(); }

void OcrService::prepare()
{
    if (m_ready || m_preparing) return;
    m_preparing = true;   // the request queues until the (auto-started) worker connects
    setStatus(QStringLiteral("loading the handwriting model…"));
    call("prepare", {{"models_dir", paths::modelsDir()}, {"model", m_lib.setting("ocr.model", "microsoft/trocr-small-handwritten")}},
         [this](const QJsonObject &r, const QJsonObject &e) {
             m_preparing = false;
             m_ready = e.isEmpty() && r.value("ok").toBool();
             setStatus(m_ready ? QString() : QStringLiteral("handwriting model unavailable: ") + e.value("message").toString());
             emit stateChanged();
             if (m_ready && !m_queue.isEmpty()) m_debounce.start();
         });
}

void OcrService::schedule(qint64 pageId)
{
    if (!pageId) return;
    m_queue.insert(pageId);
    m_debounce.start();
    emit stateChanged();
}

void OcrService::recognizeNow(qint64 pageId) { m_queue.insert(pageId); m_debounce.stop(); runNext(); }

void OcrService::scanStale(int limit)
{
    Database::Query q(m_db, "SELECT p.id FROM page p JOIN stroke_blob b ON b.page_id=p.id WHERE p.deleted_at IS NULL AND b.stroke_count>0"
                            " AND p.modified > COALESCE((SELECT MAX(updated) FROM ocr_result o WHERE o.page_id=p.id), 0) ORDER BY p.modified DESC LIMIT ?");
    q.bind(1, limit);
    while (q.step()) m_queue.insert(q.i64(0));
    if (!m_queue.isEmpty()) m_debounce.start();
    emit stateChanged();
}

void OcrService::runNext()
{
    if (m_running || m_queue.isEmpty()) return;
    if (!m_ready) { prepare(); return; }
    const qint64 pageId = *m_queue.cbegin();
    m_queue.remove(pageId);
    recognizePage(pageId);
}

void OcrService::recognizePage(qint64 pageId)
{
    QVector<Stroke> strokes;
    {
        Database::Query q(m_db, "SELECT data FROM stroke_blob WHERE page_id=?"); q.bind(1, pageId);
        if (!q.step() || !strokecodec::decode(q.blob(0), strokes)) { emit stateChanged(); runNext(); return; }
    }
    const QVector<InkLine> lines = segmentLines(strokes);
    QHash<quint64, const Stroke *> byId; for (const Stroke &s : strokes) byId.insert(s.id, &s);
    if (lines.isEmpty()) { Database::Query d(m_db, "DELETE FROM ocr_result WHERE page_id=?"); d.bind(1, pageId); d.run(); m_lib.unindex("ocr", pageId); runNext(); return; }
    // Render each line at 2× on white, the way a scanner would see it.
    const PressureCurve curve = PressureCurve::forStyle(PenStyle::Classic);
    QDir().mkpath(paths::cacheDir() + "/ocr");
    QJsonArray payload;
    QVector<QRectF> boxes;
    QVector<QString> idLists;
    for (int i = 0; i < lines.size(); ++i) {
        const InkLine &ln = lines[i];
        const QRectF b = ln.box.adjusted(-10, -10, 10, 10);
        const qreal scale = std::clamp(64.0 / std::max(ln.box.height(), 8.0), 1.0, 4.0);   // ~64 px tall lines suit TrOCR
        QImage img(int(b.width() * scale) + 1, int(b.height() * scale) + 1, QImage::Format_RGB32);
        img.fill(Qt::white);
        QPainter p(&img);
        p.setRenderHint(QPainter::Antialiasing);
        p.scale(scale, scale);
        p.translate(-b.topLeft());
        QStringList ids;
        for (quint64 id : ln.ids) {
            ids << QString::number(id);
            if (const Stroke *sp = byId.value(id)) {
                const Stroke &s = *sp;
                const QVector<InkPoint> pts = smoothStroke(s.points, 1.f);
                for (int k = 1; k < pts.size(); ++k) {
                    p.setPen(QPen(Qt::black, std::max(1.2f, curve.widthFor(s.width, (pts[k - 1].pressure + pts[k].pressure) / 2)), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
                    p.drawLine(QPointF(pts[k - 1].x, pts[k - 1].y), QPointF(pts[k].x, pts[k].y));
                }
                if (pts.size() == 1) { p.setBrush(Qt::black); p.setPen(Qt::NoPen); p.drawEllipse(QPointF(pts[0].x, pts[0].y), s.width / 2, s.width / 2); }
            }
        }
        p.end();
        const QString png = QStringLiteral("%1/ocr/p%2-l%3.png").arg(paths::cacheDir()).arg(pageId).arg(i);
        img.save(png);
        payload.append(QJsonObject{{"id", i}, {"png", png}});
        boxes.append(ln.box);
        idLists.append(ids.join(','));
    }
    m_running = true;
    setStatus(QStringLiteral("reading handwriting on page %1 (%2 lines)…").arg(pageId).arg(lines.size()));
    call("recognize_lines", {{"lines", payload}}, [this, pageId, boxes, idLists](const QJsonObject &r, const QJsonObject &e) {
        m_running = false;
        if (!e.isEmpty()) { setStatus(QString()); emit failed(e.value("message").toString()); runNext(); return; }
        const qint64 now = QDateTime::currentSecsSinceEpoch();
        m_db.begin();
        // Keep corrected lines whose ink is unchanged; replace the rest.
        Database::Query d(m_db, "DELETE FROM ocr_result WHERE page_id=? AND corrected=0"); d.bind(1, pageId); d.run();
        m_lib.unindex("ocr", pageId);
        int n = 0;
        for (const QJsonValue &v : r.value("lines").toArray()) {
            const QJsonObject ln = v.toObject();
            const int i = ln.value("id").toInt();
            const QString text = ln.value("text").toString().trimmed();
            if (i < 0 || i >= boxes.size() || text.isEmpty()) continue;
            Database::Query c(m_db, "SELECT id, text FROM ocr_result WHERE page_id=? AND corrected=1 AND stroke_ids=?"); c.bind(1, pageId).bind(2, idLists[i]);
            if (c.step()) { m_lib.indexText("ocr", pageId, c.i64(0), c.text(1)); ++n; continue; }
            Database::Query q(m_db, "INSERT INTO ocr_result(page_id, x, y, w, h, text, confidence, stroke_ids, updated) VALUES (?,?,?,?,?,?,?,?,?)");
            q.bind(1, pageId).bind(2, boxes[i].x()).bind(3, boxes[i].y()).bind(4, boxes[i].width()).bind(5, boxes[i].height()).bind(6, text).bind(7, ln.value("confidence").toDouble()).bind(8, idLists[i]).bind(9, now);
            q.run();
            m_lib.indexText("ocr", pageId, m_db.lastInsertId(), text);
            ++n;
        }
        m_db.commit();
        { Database::Query f(m_db, "SELECT text FROM ocr_result WHERE page_id=? ORDER BY y, x LIMIT 1"); f.bind(1, pageId); if (f.step()) m_lib.suggestTitle(pageId, f.text(0)); }
        setStatus(QString());
        emit pageRecognized(pageId, n);
        emit stateChanged();
        QTimer::singleShot(500, this, &OcrService::runNext);
        m_idleUnload.start();
    });
}

QVariantList OcrService::results(qint64 pageId) const
{
    QVariantList out;
    Database::Query q(m_db, "SELECT id, x, y, w, h, text, confidence, corrected FROM ocr_result WHERE page_id=? ORDER BY y, x");
    q.bind(1, pageId);
    while (q.step())
        out.append(QVariantMap{{"id", q.i64(0)}, {"x", q.f64(1)}, {"y", q.f64(2)}, {"w", q.f64(3)}, {"h", q.f64(4)}, {"text", q.text(5)}, {"confidence", q.f64(6)}, {"corrected", q.i32(7) != 0}});
    return out;
}

void OcrService::correct(qint64 resultId, const QString &text)
{
    qint64 pageId = 0;
    { Database::Query q(m_db, "SELECT page_id FROM ocr_result WHERE id=?"); q.bind(1, resultId); if (q.step()) pageId = q.i64(0); }
    if (!pageId) return;
    Database::Query u(m_db, "UPDATE ocr_result SET text=?, corrected=1, confidence=1, updated=? WHERE id=?");
    u.bind(1, text).bind(2, QDateTime::currentSecsSinceEpoch()).bind(3, resultId); u.run();
    m_lib.indexText("ocr", pageId, resultId, text);
    emit pageRecognized(pageId, -1);
}

void OcrService::latexFromImage(const QString &png)
{
    if (png.isEmpty()) { emit failed("lasso some handwritten maths first"); return; }
    setStatus(QStringLiteral("reading the maths…"));
    call("latex", {{"png", png}}, [this, png](const QJsonObject &r, const QJsonObject &e) {
        setStatus(QString());
        if (!e.isEmpty()) { emit failed(QStringLiteral("LaTeX recognition failed: ") + e.value("message").toString()); return; }
        emit latexReady(r.value("latex").toString(), png);
    });
}
