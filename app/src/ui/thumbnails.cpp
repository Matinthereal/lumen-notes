#include "thumbnails.h"
#include "ink/tessellate.h"
#include "storage/attachments.h"
#include "storage/database.h"
#include "storage/paths.h"
#include "storage/strokecodec.h"
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QPainter>
#include <QThreadPool>

Thumbnails::Thumbnails(Database &db, QObject *parent) : QObject(parent), m_db(db) { QDir().mkpath(paths::cacheDir() + "/thumbs"); }

QString Thumbnails::pathFor(qint64 pageId) const
{
    const QString p = QStringLiteral("%1/thumbs/%2.png").arg(paths::cacheDir()).arg(pageId);
    return QFileInfo::exists(p) ? p : QString();
}

void Thumbnails::ensure(qint64 pageId) { if (pathFor(pageId).isEmpty()) refresh(pageId); }

void Thumbnails::refresh(qint64 pageId) { if (!m_inFlight.contains(pageId)) render(pageId); }

void Thumbnails::render(qint64 pageId)
{
    // Read on the GUI thread (SQLite connection is not shared), paint on the pool.
    QVector<Stroke> strokes;
    double pw = 794, ph = 1123; bool pdf = false; QString style = "dotted"; QString paper;
    struct Pic { QString path; QRectF rect; };
    QVector<Pic> pics;
    struct Shp { QString kind, stroke, fill; QRectF rect; double width; };
    QVector<Shp> shps;
    {
        Database::Query q(m_db, "SELECT p.width, p.height, p.style, (SELECT COUNT(*) FROM pdf_page x WHERE x.page_id=p.id), b.data, p.paper FROM page p LEFT JOIN stroke_blob b ON b.page_id=p.id WHERE p.id=?");
        q.bind(1, pageId);
        if (!q.step()) return;
        pw = q.f64(0); ph = q.f64(1); style = q.text(2); pdf = q.i32(3) > 0;
        paper = q.text(5);
        if (!q.isNull(4)) strokecodec::decode(q.blob(4), strokes);
    }
    if (paper.isEmpty()) {          // the page follows the app default; the thumbnail must too
        Database::Query d(m_db, "SELECT value FROM setting WHERE key='page.paper'");
        if (d.step()) paper = d.text(0);
        if (paper.isEmpty()) paper = QStringLiteral("#22262B");   // the same default Main.qml uses
    }
    {
        Database::Query q(m_db, "SELECT i.attachment, a.mime, i.x, i.y, i.w, i.h FROM image i JOIN attachment a ON a.sha256=i.attachment WHERE i.page_id=? ORDER BY i.id");
        q.bind(1, pageId);
        while (q.step()) pics.append({attachments::pathFor(q.text(0), q.text(1)), QRectF(q.f64(2), q.f64(3), q.f64(4), q.f64(5))});
    }
    {
        Database::Query q(m_db, "SELECT kind, x, y, w, h, stroke, fill, width FROM shape WHERE page_id=? ORDER BY sort, id");
        q.bind(1, pageId);
        while (q.step()) shps.append({q.text(0), q.text(5), q.text(6), QRectF(q.f64(1), q.f64(2), q.f64(3), q.f64(4)), q.f64(7)});
    }
    m_inFlight.insert(pageId);
    const QString out = QStringLiteral("%1/thumbs/%2.png").arg(paths::cacheDir()).arg(pageId);
    QThreadPool::globalInstance()->start([this, pageId, strokes, pw, ph, pdf, style, out, pics, shps, paper] {
        const int W = 160, H = int(160 * ph / pw);
        QImage img(W, H, QImage::Format_ARGB32_Premultiplied);
        const QColor sheet = paper.isEmpty() ? (pdf ? QColor(0xF3, 0xF4, 0xF6) : QColor(Qt::white)) : QColor(paper);
        img.fill(sheet.isValid() ? sheet : QColor(Qt::white));
        const bool darkPaper = sheet.isValid() && sheet.lightnessF() < 0.45;
        QPainter p(&img);
        p.setRenderHint(QPainter::Antialiasing);
        const double s = double(W) / pw;
        if (style != "plain" && !pdf) {                    // faint guide hint
            p.setPen(QPen(darkPaper ? QColor(255, 255, 255, 30) : QColor(0, 0, 0, 22), 1));
            const double pitch = (style == "lined" || style == "cornell") ? 30 * s : 19 * s;
            for (double y = pitch; y < H; y += pitch) p.drawLine(QPointF(0, y), QPointF(W, y));
        }
        p.scale(s, s);
        for (const Pic &pic : pics) {                      // pictures sit under the ink here too
            QImageReader reader(pic.path);
            reader.setAutoTransform(true);
            reader.setScaledSize(QSize(std::max(1, int(pic.rect.width() * s)), std::max(1, int(pic.rect.height() * s))));
            const QImage loaded = reader.read();
            if (!loaded.isNull()) p.drawImage(pic.rect, loaded);
        }
        const PressureCurve curve = PressureCurve::forStyle(PenStyle::Classic);
        for (const Stroke &st : strokes) {
            QColor c = QColor::fromRgba(st.color);
            if (st.tool == InkTool::Highlighter) c.setAlphaF(0.35);
            const QVector<InkPoint> pts = smoothStroke(st.points, 4.f);
            const float w = std::max(1.2f / float(s), st.tool == InkTool::Highlighter ? st.width : curve.widthFor(st.width, 0.5f));
            p.setPen(QPen(c, w, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            for (int i = 1; i < pts.size(); ++i) p.drawLine(QPointF(pts[i - 1].x, pts[i - 1].y), QPointF(pts[i].x, pts[i].y));
            if (pts.size() == 1) { p.setBrush(c); p.setPen(Qt::NoPen); p.drawEllipse(QPointF(pts[0].x, pts[0].y), w / 2, w / 2); }
        }
        for (const Shp &sh : shps) {              // above the ink, as on screen
            QPen pen(QColor(sh.stroke.isEmpty() ? "#1A1A1A" : sh.stroke), std::max(0.6, sh.width), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
            p.setPen(pen);
            p.setBrush(sh.fill.isEmpty() ? Qt::NoBrush : QBrush(QColor(sh.fill)));
            const QRectF r = sh.rect.normalized();
            if (sh.kind == "ellipse") p.drawEllipse(r);
            else if (sh.kind == "triangle") { const QPointF pts[3] = {{r.center().x(), r.top()}, {r.right(), r.bottom()}, {r.left(), r.bottom()}}; p.drawPolygon(pts, 3); }
            else if (sh.kind == "line" || sh.kind == "arrow") p.drawLine(sh.rect.topLeft(), sh.rect.topLeft() + QPointF(sh.rect.width(), sh.rect.height()));
            else p.drawRect(r);
        }
        p.end();
        img.save(out + ".part", "PNG");                    // explicit format: the suffix is not .png
        QFile::remove(out);
        QFile::rename(out + ".part", out);
        QMetaObject::invokeMethod(this, [this, pageId] { m_inFlight.remove(pageId); m_version[pageId]++; emit changed(pageId); }, Qt::QueuedConnection);
    });
}
