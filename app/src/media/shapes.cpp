#include "shapes.h"
#include "storage/database.h"

#include <algorithm>

Shapes::Shapes(Database &db, QObject *parent) : QObject(parent), m_db(db) {}

static QVariantMap rowToMap(Database::Query &q)
{
    return QVariantMap{{"id", q.i64(0)}, {"kind", q.text(1)}, {"x", q.f64(2)}, {"y", q.f64(3)},
                       {"w", q.f64(4)}, {"h", q.f64(5)}, {"stroke", q.text(6)}, {"fill", q.text(7)},
                       {"width", q.f64(8)}, {"pageId", q.i64(9)}};
}

QVariantList Shapes::list(qint64 pageId) const
{
    QVariantList out;
    Database::Query q(m_db, "SELECT id, kind, x, y, w, h, stroke, fill, width, page_id FROM shape WHERE page_id=? ORDER BY sort, id");
    q.bind(1, pageId);
    while (q.step()) out.append(rowToMap(q));
    return out;
}

QVariantMap Shapes::shape(qint64 id) const
{
    Database::Query q(m_db, "SELECT id, kind, x, y, w, h, stroke, fill, width, page_id FROM shape WHERE id=?");
    q.bind(1, id);
    return q.step() ? rowToMap(q) : QVariantMap{};
}

int Shapes::count(qint64 pageId) const
{
    Database::Query q(m_db, "SELECT COUNT(*) FROM shape WHERE page_id=?");
    q.bind(1, pageId);
    return q.step() ? q.i32(0) : 0;
}

qint64 Shapes::create(qint64 pageId, const QString &kind, double x, double y, double w, double h,
                      const QString &stroke, const QString &fill, double width)
{
    if (!pageId) return 0;
    // A shape you can never grab again is a bug, so nothing may be created thinner than a fingertip.
    // A shape you can never grab again is a bug, so nothing may be created thinner than a fingertip —
    // but lines and arrows carry their direction in the sign of w/h, and clamping ate it (D-064).
    const bool signed_ = (kind == QLatin1String("line") || kind == QLatin1String("arrow"));
    const auto keep = [signed_](double v, double floor) {
        if (!signed_) return std::max(floor, v);
        return v < 0 ? -std::max(0.0, -v) : std::max(0.0, v);
    };
    const double ww = keep(w, 4.0), hh = keep(h, signed_ ? 0.0 : 4.0);
    int sort = 0;
    { Database::Query q(m_db, "SELECT COALESCE(MAX(sort), -1) + 1 FROM shape WHERE page_id=?"); q.bind(1, pageId); if (q.step()) sort = q.i32(0); }
    Database::Query q(m_db, "INSERT INTO shape(page_id, kind, x, y, w, h, stroke, fill, width, sort) VALUES (?,?,?,?,?,?,?,?,?,?)");
    q.bind(1, pageId).bind(2, kind).bind(3, x).bind(4, y).bind(5, ww).bind(6, hh)
     .bind(7, stroke).bind(8, fill).bind(9, std::clamp(width, 0.5, 40.0)).bind(10, sort);
    if (!q.run()) return 0;
    emit changed(pageId);
    return m_db.lastInsertId();
}

void Shapes::setGeometry(qint64 id, double x, double y, double w, double h)
{
    const QVariantMap before = shape(id);
    if (before.isEmpty()) return;
    Database::Query q(m_db, "UPDATE shape SET x=?, y=?, w=?, h=? WHERE id=?");
    q.bind(1, x).bind(2, y).bind(3, w).bind(4, h).bind(5, id);
    if (q.run()) emit changed(before.value("pageId").toLongLong());
}

void Shapes::setStyle(qint64 id, const QString &stroke, const QString &fill, double width)
{
    const QVariantMap before = shape(id);
    if (before.isEmpty()) return;
    Database::Query q(m_db, "UPDATE shape SET stroke=?, fill=?, width=? WHERE id=?");
    q.bind(1, stroke.isEmpty() ? before.value("stroke").toString() : stroke)
     .bind(2, fill)                                     // empty means no fill, which is a real choice
     .bind(3, width > 0 ? std::clamp(width, 0.5, 40.0) : before.value("width").toDouble())
     .bind(4, id);
    if (q.run()) emit changed(before.value("pageId").toLongLong());
}

void Shapes::remove(qint64 id)
{
    const QVariantMap before = shape(id);
    if (before.isEmpty()) return;
    Database::Query q(m_db, "DELETE FROM shape WHERE id=?");
    q.bind(1, id);
    if (q.run()) emit changed(before.value("pageId").toLongLong());
}

qint64 Shapes::duplicate(qint64 id)
{
    const QVariantMap s = shape(id);
    if (s.isEmpty()) return 0;
    return create(s.value("pageId").toLongLong(), s.value("kind").toString(),
                  s.value("x").toDouble() + 18, s.value("y").toDouble() + 18,
                  s.value("w").toDouble(), s.value("h").toDouble(),
                  s.value("stroke").toString(), s.value("fill").toString(), s.value("width").toDouble());
}
