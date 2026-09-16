#include "linesegment.h"
#include <algorithm>
#include <QHash>

QVector<InkLine> segmentLines(const QVector<Stroke> &strokes)
{
    struct Item { quint64 id; QRectF box; };
    QVector<Item> items;
    for (const Stroke &s : strokes)
        if (s.tool == InkTool::Pen && !s.points.isEmpty()) items.append({s.id, s.bounds.adjusted(2, 2, -2, -2)});
    std::sort(items.begin(), items.end(), [](const Item &a, const Item &b) { return a.box.center().y() < b.box.center().y(); });
    QVector<InkLine> lines;
    for (const Item &it : items) {
        InkLine *best = nullptr; double bestOverlap = 0;
        for (InkLine &ln : lines) {
            const double top = std::max(ln.box.top(), it.box.top()), bottom = std::min(ln.box.bottom(), it.box.bottom());
            const double overlap = bottom - top;
            const double ref = std::min(ln.box.height(), it.box.height());
            const bool centreInside = it.box.center().y() >= ln.box.top() && it.box.center().y() <= ln.box.bottom();
            if ((ref > 0 && overlap / ref > 0.4) || centreInside)
                if (overlap > bestOverlap) { bestOverlap = overlap; best = &ln; }
        }
        if (best) { best->ids.append(it.id); best->box |= it.box; }
        else lines.append({{it.id}, it.box});
    }
    // Tiny isolated marks (dots, i-tittles) join the nearest line above/below within one line height.
    for (int i = lines.size() - 1; i >= 0; --i) {
        if (lines[i].ids.size() == 1 && lines[i].box.height() < 8 && lines[i].box.width() < 8) {
            int j = -1; double bd = 1e18;
            for (int k = 0; k < lines.size(); ++k) if (k != i) { const double d = std::abs(lines[k].box.center().y() - lines[i].box.center().y()); if (d < bd && d < lines[k].box.height() * 1.2) { bd = d; j = k; } }
            if (j >= 0) { lines[j].ids += lines[i].ids; lines[j].box |= lines[i].box; lines.removeAt(i); }
        }
    }
    // left → right inside each line (one lookup table, not a scan per comparison)
    QHash<quint64, qreal> leftOf;
    for (const Stroke &s : strokes) leftOf.insert(s.id, s.bounds.left());
    for (InkLine &ln : lines)
        std::sort(ln.ids.begin(), ln.ids.end(), [&](quint64 a, quint64 b) { return leftOf.value(a) < leftOf.value(b); });
    std::sort(lines.begin(), lines.end(), [](const InkLine &a, const InkLine &b) { return a.box.top() < b.box.top(); });
    return lines;
}
