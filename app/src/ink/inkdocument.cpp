#include "inkdocument.h"
#include "geometry.h"
#include <QSet>

InkDocument::InkDocument(QObject *parent) : QObject(parent) {}

const Stroke *InkDocument::stroke(quint64 id) const
{
    const int i = m_index.value(id, -1);
    return i >= 0 ? &m_strokes[i] : nullptr;
}

void InkDocument::reindexFrom(int index)
{
    for (int i = index; i < m_strokes.size(); ++i)
        m_index[m_strokes[i].id] = i;
}

quint64 InkDocument::addStroke(Stroke s)
{
    if (s.id == 0) s.id = nextId(); else m_nextId = std::max(m_nextId, s.id + 1);
    const quint64 id = s.id;
    insertStroke(m_strokes.size(), std::move(s));
    return id;
}

void InkDocument::insertStroke(int index, Stroke s)
{
    if (s.id == 0) s.id = nextId(); else m_nextId = std::max(m_nextId, s.id + 1);
    index = std::clamp(index, 0, int(m_strokes.size()));
    s.updateBounds();
    const quint64 id = s.id;
    m_strokes.insert(index, std::move(s));
    reindexFrom(index);
    emit strokeAdded(id, index);
}

bool InkDocument::removeStroke(quint64 id, Stroke *removed, int *index)
{
    const int i = m_index.value(id, -1);
    if (i < 0) return false;
    if (removed) *removed = m_strokes[i];
    if (index) *index = i;
    m_strokes.removeAt(i);
    m_index.remove(id);
    reindexFrom(i);
    emit strokeRemoved(id, i);
    return true;
}

QVector<std::pair<int, Stroke>> InkDocument::removeStrokes(const QVector<quint64> &ids)
{
    QVector<std::pair<int, Stroke>> removed;
    QSet<quint64> want; for (quint64 id : ids) if (m_index.contains(id)) want.insert(id);
    if (want.isEmpty()) return removed;                 // nothing to do — and nothing may be moved out of m_strokes
    QVector<Stroke> kept; kept.reserve(m_strokes.size());
    for (int i = 0; i < m_strokes.size(); ++i) {
        if (want.contains(m_strokes[i].id)) removed.append({i, m_strokes[i]}); else kept.append(std::move(m_strokes[i]));
    }
    m_strokes = std::move(kept);
    m_index.clear();
    reindexFrom(0);
    for (const auto &r : removed) emit strokeRemoved(r.second.id, r.first);
    return removed;
}

void InkDocument::transformStrokes(const QVector<quint64> &ids, const QTransform &t)
{
    QVector<quint64> changed;
    for (quint64 id : ids) {
        const int i = m_index.value(id, -1);
        if (i < 0) continue;
        Stroke &s = m_strokes[i];
        for (InkPoint &p : s.points) {
            const QPointF q = t.map(QPointF(p.x, p.y));
            p.x = float(q.x()); p.y = float(q.y());
        }
        // A uniform scale changes the width too; use the mean axis scale.
        const float k = float(std::sqrt(std::abs(t.determinant())));
        if (k > 0 && std::abs(k - 1.f) > 1e-4f) s.width *= k;
        s.updateBounds();
        changed.append(id);
    }
    if (!changed.isEmpty()) emit strokesChanged(changed);
}

bool InkDocument::updateStroke(const Stroke &s)
{
    const int i = m_index.value(s.id, -1);
    if (i < 0) return false;
    m_strokes[i] = s;
    m_strokes[i].updateBounds();
    emit strokesChanged({s.id});
    return true;
}

void InkDocument::clear()
{
    m_strokes.clear();
    m_index.clear();
    emit cleared();
}

QVector<quint64> InkDocument::hitCircle(QPointF c, float r) const
{
    QVector<quint64> hits;
    const QRectF probe(c.x() - r, c.y() - r, 2 * r, 2 * r);
    for (const Stroke &s : m_strokes) {
        if (!s.bounds.intersects(probe)) continue;
        const float reach = r + s.width * 0.75f;
        const float reach2 = reach * reach;
        const int n = s.points.size();
        bool hit = false;
        if (n == 1) {
            const float dx = s.points[0].x - float(c.x()), dy = s.points[0].y - float(c.y());
            hit = dx * dx + dy * dy <= reach2;
        }
        for (int i = 0; i + 1 < n && !hit; ++i)
            hit = inkgeo::distSqPointSegment(float(c.x()), float(c.y()), s.points[i].x, s.points[i].y,
                                             s.points[i + 1].x, s.points[i + 1].y) <= reach2;
        if (hit) hits.append(s.id);
    }
    return hits;
}

QVector<quint64> InkDocument::insidePolygon(const QPolygonF &poly, float minFraction) const
{
    QVector<quint64> out;
    if (poly.size() < 3) return out;
    const QRectF pb = poly.boundingRect();
    for (const Stroke &s : m_strokes) {
        if (!s.bounds.intersects(pb) || s.points.isEmpty()) continue;
        int in = 0;
        for (const InkPoint &p : s.points)
            if (inkgeo::pointInPolygon(poly, p.x, p.y)) ++in;
        if (float(in) / s.points.size() >= minFraction) out.append(s.id);
    }
    return out;
}

QRectF InkDocument::boundsOf(const QVector<quint64> &ids) const
{
    QRectF r;
    for (quint64 id : ids)
        if (const Stroke *s = stroke(id)) r = r.isNull() ? s->bounds : r.united(s->bounds);
    return r;
}

QVector<Stroke> InkDocument::splitByCircle(const Stroke &s, QPointF c, float r)
{
    QVector<Stroke> pieces;
    const float reach = r + s.width * 0.5f, reach2 = reach * reach;
    Stroke cur = s; cur.id = 0; cur.points.clear();
    auto flush = [&] {
        if (!cur.points.isEmpty()) { cur.updateBounds(); pieces.append(cur); cur.points.clear(); }
    };
    for (const InkPoint &p : s.points) {
        const float dx = p.x - float(c.x()), dy = p.y - float(c.y());
        if (dx * dx + dy * dy <= reach2) flush(); else cur.points.append(p);
    }
    flush();
    // A single leftover point is dust, not ink.
    pieces.erase(std::remove_if(pieces.begin(), pieces.end(), [](const Stroke &p) { return p.points.size() < 2; }), pieces.end());
    return pieces;
}
